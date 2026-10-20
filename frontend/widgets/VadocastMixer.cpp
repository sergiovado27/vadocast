/******************************************************************************
    vadocast - simple sound mixer ("Sonido" panel)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include "VadocastMixer.hpp"

#include <widgets/OBSBasic.hpp>

#include <OBSApp.hpp>
#include <qt-wrappers.hpp>

#include <QAction>
#include <QColor>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <initializer_list>
#include <cmath>
#include <cstring>

#include "moc_VadocastMixer.cpp"

namespace {

constexpr int kGlobalChannelFirst = 1;
constexpr int kGlobalChannelLast = 6;
constexpr int kSliderPrecision = 1000;
constexpr int kSectionCount = 4;

/* DESIGN.md colour tokens */
const QColor kColorText(0xFF, 0xFF, 0xFF);
const QColor kColorMuted(0x96, 0x96, 0x96);
const QColor kColorRed(0xE5, 0x09, 0x14);
const QColor kColorTrack(0x2C, 0x2C, 0x2C);

/* Renders an existing monochrome resource icon in a single colour. */
QIcon tintedIcon(const QString &resource, const QColor &color)
{
	QImage image(32, 32, QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);

	QPainter painter(&image);
	QIcon(resource).paint(&painter, QRect(0, 0, 32, 32));
	painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
	painter.fillRect(image.rect(), color);
	painter.end();

	return QIcon(QPixmap::fromImage(image));
}

bool privateBool(obs_source_t *source, const char *name)
{
	OBSDataAutoRelease priv = obs_source_get_private_settings(source);
	return obs_data_get_bool(priv, name);
}

/* Same storage as the stock mixer (VolumeControl::setHiddenInMixer). */
bool isHiddenInMixer(obs_source_t *source)
{
	return privateBool(source, "mixer_hidden");
}

bool isPinnedInMixer(obs_source_t *source)
{
	return privateBool(source, "mixer_pinned");
}

bool isVolumeLocked(obs_source_t *source)
{
	return privateBool(source, "volume_locked");
}

QString sourceUuidOf(obs_source_t *source)
{
	const char *uuid = source ? obs_source_get_uuid(source) : nullptr;
	return (uuid && *uuid) ? QString::fromUtf8(uuid) : QString();
}

bool idIs(const char *id, std::initializer_list<const char *> list)
{
	if (!id)
		return false;
	for (const char *candidate : list) {
		if (strcmp(id, candidate) == 0)
			return true;
	}
	return false;
}

/* globalChannel: 0 when the source is not a global audio channel. */
VadocastMixerItem::Kind classify(obs_source_t *source, int globalChannel)
{
	const char *id = obs_source_get_unversioned_id(source);

	if (idIs(id, {"wasapi_input_capture", "coreaudio_input_capture", "pulse_input_capture", "alsa_input_capture"}))
		return VadocastMixerItem::Kind::Microphone;

	if (idIs(id, {"wasapi_output_capture", "wasapi_process_output_capture", "coreaudio_output_capture",
		      "pulse_output_capture", "jack_output_capture", "sck_audio_capture"}))
		return VadocastMixerItem::Kind::Desktop;

	if (idIs(id, {"ffmpeg_source", "vlc_source", "browser_source"}))
		return VadocastMixerItem::Kind::Media;

	/* Unknown type assigned to a global channel: 1-2 are desktop, 3-6 mic/aux */
	if (globalChannel == 1 || globalChannel == 2)
		return VadocastMixerItem::Kind::Desktop;
	if (globalChannel >= 3)
		return VadocastMixerItem::Kind::Microphone;

	return VadocastMixerItem::Kind::Other;
}

int sectionIndex(VadocastMixerItem::Kind kind)
{
	switch (kind) {
	case VadocastMixerItem::Kind::Microphone:
		return 0;
	case VadocastMixerItem::Kind::Desktop:
		return 1;
	case VadocastMixerItem::Kind::Media:
		return 2;
	case VadocastMixerItem::Kind::Other:
	default:
		return 3;
	}
}

/* Small horizontal level meter shown when a card is expanded. */
class VcLevelBar : public QWidget {
public:
	explicit VcLevelBar(QWidget *parent) : QWidget(parent)
	{
		setFixedHeight(6);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	}

	void setLevel(float value)
	{
		value = std::clamp(value, 0.0f, 1.0f);
		if (std::fabs(value - level) > 0.001f) {
			level = value;
			update();
		}
	}

	void setMuted(bool value)
	{
		if (muted != value) {
			muted = value;
			update();
		}
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing);
		painter.setPen(Qt::NoPen);

		const QRectF track = QRectF(rect());
		const qreal radius = track.height() / 2.0;
		painter.setBrush(kColorTrack);
		painter.drawRoundedRect(track, radius, radius);

		if (level <= 0.0f)
			return;

		QRectF fill = track;
		fill.setWidth(track.width() * level);
		QColor color = muted ? kColorMuted : (level > 0.95f ? kColorRed : kColorText);
		painter.setBrush(color);
		painter.drawRoundedRect(fill, radius, radius);
	}

private:
	float level = 0.0f;
	bool muted = false;
};

} // namespace

/* ------------------------------------------------------------------------ */
/* VadocastMixerItem                                                         */
/* ------------------------------------------------------------------------ */

VadocastMixerItem::VadocastMixerItem(obs_source_t *source, Kind kind, QWidget *parent)
	: QFrame(parent),
	  weakSource(OBSGetWeakRef(source)),
	  sourceUuid(sourceUuidOf(source)),
	  fullName(QString::fromUtf8(obs_source_get_name(source))),
	  sourceKind(kind)
{
	setAttribute(Qt::WA_StyledBackground, true);
	setClasses(this, "vc-card");
	setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

	expandButton = new QToolButton(this);
	expandButton->setCheckable(true);
	expandButton->setAutoRaise(true);
	expandButton->setIconSize(QSize(10, 10));
	expandButton->setIcon(tintedIcon(":/res/images/right.svg", kColorMuted));
	expandButton->setToolTip(QTStr("Vadocast.Mixer.ShowLevel"));
	setClasses(expandButton, "vc-icon-button");

	nameLabel = new QLabel(this);
	nameLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	nameLabel->setMinimumWidth(20);
	setClasses(nameLabel, "vc-label");

	muteButton = new QToolButton(this);
	muteButton->setAutoRaise(true);
	muteButton->setIconSize(QSize(16, 16));
	setClasses(muteButton, "vc-icon-button");

	menuButton = new QToolButton(this);
	menuButton->setAutoRaise(true);
	menuButton->setIconSize(QSize(16, 16));
	menuButton->setIcon(tintedIcon(":/res/images/dots.svg", kColorMuted));
	menuButton->setToolTip(QTStr("Vadocast.Mixer.MoreOptions"));
	setClasses(menuButton, "vc-icon-button");

	slider = new QSlider(Qt::Horizontal, this);
	slider->setRange(0, kSliderPrecision);
	slider->setPageStep(kSliderPrecision / 20);
	slider->setSingleStep(kSliderPrecision / 100);
	slider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	setClasses(slider, "vc-slider");

	valueLabel = new QLabel(this);
	valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	valueLabel->setMinimumWidth(44);
	setClasses(valueLabel, "vc-value");

	meter = new VcLevelBar(this);
	meter->setVisible(false);

	QHBoxLayout *topRow = new QHBoxLayout();
	topRow->setContentsMargins(0, 0, 0, 0);
	topRow->setSpacing(6);
	topRow->addWidget(expandButton);
	topRow->addWidget(nameLabel, 1);
	topRow->addWidget(muteButton);
	topRow->addWidget(menuButton);

	QHBoxLayout *sliderRow = new QHBoxLayout();
	sliderRow->setContentsMargins(0, 0, 0, 0);
	sliderRow->setSpacing(10);
	sliderRow->addWidget(slider, 1);
	sliderRow->addWidget(valueLabel);

	QVBoxLayout *mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(0, 0, 0, 0);
	mainLayout->setSpacing(6);
	mainLayout->addLayout(topRow);
	mainLayout->addLayout(sliderRow);
	mainLayout->addWidget(meter);

	/* Perceptual fader, same mapping as the stock mixer: 100% = 0 dB */
	fader = obs_fader_create(OBS_FADER_LOG);
	obs_fader_add_callback(fader, VadocastMixerItem::obsFaderChanged, this);
	obs_fader_attach_source(fader, source);

	sourceSignals.emplace_back(obs_source_get_signal_handler(source), "mute", VadocastMixerItem::obsSourceMuted,
				   this);

	meterTimer.setInterval(33);
	connect(&meterTimer, &QTimer::timeout, this, &VadocastMixerItem::updateMeter);

	connect(expandButton, &QToolButton::toggled, this, [this](bool checked) { setExpanded(checked); });
	connect(muteButton, &QToolButton::clicked, this, &VadocastMixerItem::muteClicked);
	connect(menuButton, &QToolButton::clicked, this, [this]() { showMenu(); });
	connect(slider, &QSlider::valueChanged, this, &VadocastMixerItem::sliderMoved);

	muted = obs_source_muted(source);
	setVolumeLocked(isVolumeLocked(source));
	updateElidedName();
	syncVolumeFromSource();
	updateMuteButton();
}

VadocastMixerItem::~VadocastMixerItem()
{
	meterTimer.stop();
	sourceSignals.clear();

	if (volmeter) {
		obs_volmeter_remove_callback(volmeter, VadocastMixerItem::obsVolmeterUpdated, this);
		obs_volmeter_destroy(volmeter);
		volmeter = nullptr;
	}

	if (fader) {
		obs_fader_remove_callback(fader, VadocastMixerItem::obsFaderChanged, this);
		obs_fader_destroy(fader);
		fader = nullptr;
	}
}

void VadocastMixerItem::setKind(Kind kind)
{
	if (sourceKind != kind) {
		sourceKind = kind;
		updateMuteButton();
	}
}

void VadocastMixerItem::setDisplayName(const QString &name)
{
	if (fullName != name) {
		fullName = name;
		updateElidedName();
	}
}

void VadocastMixerItem::setHiddenInMixer(bool hidden)
{
	hiddenInMixer = hidden;
	nameLabel->setEnabled(!hidden);
}

void VadocastMixerItem::setVolumeLocked(bool locked)
{
	slider->setEnabled(!locked);
}

void VadocastMixerItem::updateElidedName()
{
	const int available = std::max(nameLabel->width(), 20);
	nameLabel->setText(nameLabel->fontMetrics().elidedText(fullName, Qt::ElideRight, available));
	nameLabel->setToolTip(fullName);
	slider->setAccessibleName(fullName);
}

void VadocastMixerItem::resizeEvent(QResizeEvent *event)
{
	QFrame::resizeEvent(event);
	updateElidedName();
}

void VadocastMixerItem::updateMuteButton()
{
	const bool mic = sourceKind == Kind::Microphone;

	if (mic) {
		muteButton->setIcon(tintedIcon(":/res/images/sources/microphone.svg", muted ? kColorRed : kColorText));
	} else {
		muteButton->setIcon(muted ? tintedIcon(":/res/images/mute.svg", kColorRed)
					  : tintedIcon(":/settings/images/settings/audio.svg", kColorText));
	}

	muteButton->setToolTip(QTStr(muted ? "Vadocast.Mixer.Unmute" : "Vadocast.Mixer.Mute"));
	muteButton->setAccessibleName(QTStr(muted ? "Vadocast.Mixer.Unmute" : "Vadocast.Mixer.Mute"));

	if (meter)
		static_cast<VcLevelBar *>(meter)->setMuted(muted);
}

void VadocastMixerItem::updateValueLabel()
{
	const int percent = (int)std::lround(slider->value() * 100.0 / kSliderPrecision);
	valueLabel->setText(QString("%1%").arg(percent));
}

void VadocastMixerItem::syncVolumeFromSource()
{
	if (!fader)
		return;

	const float def = std::clamp(obs_fader_get_deflection(fader), 0.0f, 1.0f);
	{
		QSignalBlocker blocker(slider);
		slider->setValue((int)std::lround(def * kSliderPrecision));
	}
	updateValueLabel();
}

void VadocastMixerItem::syncMuteFromSource()
{
	OBSSource source = OBSGetStrongRef(weakSource);
	if (!source)
		return;

	muted = obs_source_muted(source);
	updateMuteButton();
}

void VadocastMixerItem::sliderMoved(int value)
{
	if (fader)
		obs_fader_set_deflection(fader, float(value) / float(kSliderPrecision));
	updateValueLabel();
}

void VadocastMixerItem::muteClicked()
{
	OBSSource source = OBSGetStrongRef(weakSource);
	if (!source)
		return;

	obs_source_set_muted(source, !obs_source_muted(source));
	/* The "mute" signal updates the button. */
}

void VadocastMixerItem::setExpanded(bool expanded)
{
	expandButton->setIcon(tintedIcon(expanded ? ":/res/images/down.svg" : ":/res/images/right.svg", kColorMuted));
	expandButton->setToolTip(QTStr(expanded ? "Vadocast.Mixer.HideLevel" : "Vadocast.Mixer.ShowLevel"));

	if (expanded && !volmeter) {
		OBSSource source = OBSGetStrongRef(weakSource);
		if (source) {
			peakDb = -100.0f;
			shownLevel = 0.0f;
			volmeter = obs_volmeter_create(OBS_FADER_LOG);
			obs_volmeter_add_callback(volmeter, VadocastMixerItem::obsVolmeterUpdated, this);
			obs_volmeter_attach_source(volmeter, source);
			meterTimer.start();
		}
	} else if (!expanded && volmeter) {
		meterTimer.stop();
		obs_volmeter_remove_callback(volmeter, VadocastMixerItem::obsVolmeterUpdated, this);
		obs_volmeter_destroy(volmeter);
		volmeter = nullptr;
		static_cast<VcLevelBar *>(meter)->setLevel(0.0f);
	}

	meter->setVisible(expanded);
}

void VadocastMixerItem::updateMeter()
{
	/* Peak in dBFS (already includes the source volume) mapped to 0..1 over a 60 dB range */
	const float db = peakDb.exchange(-100.0f);
	float target = std::isfinite(db) ? (db + 60.0f) / 60.0f : 0.0f;
	target = std::clamp(target, 0.0f, 1.0f);

	/* Fast attack, gentle release */
	shownLevel = target >= shownLevel ? target : std::max(target, shownLevel - 0.04f);
	static_cast<VcLevelBar *>(meter)->setLevel(shownLevel);
}

void VadocastMixerItem::showMenu()
{
	/* Non-blocking popup: no nested event loop, so a refresh that deletes this
	 * card while the menu is open cannot leave a dangling frame on the stack.
	 * The menu is a child of the card and dies with it. */
	QMenu *menu = new QMenu(this);
	menu->setAttribute(Qt::WA_DeleteOnClose);

	QAction *filtersAction = menu->addAction(QTStr("Vadocast.Mixer.Filters"));
	QAction *hideAction =
		menu->addAction(QTStr(hiddenInMixer ? "Vadocast.Mixer.ShowInMixer" : "Vadocast.Mixer.HideFromMixer"));
	menu->addSeparator();
	QAction *propertiesAction = menu->addAction(QTStr("Vadocast.Mixer.Properties"));

	connect(filtersAction, &QAction::triggered, this, [this]() {
		OBSSource source = OBSGetStrongRef(weakSource);
		OBSBasic *main = OBSBasic::Get();
		if (source && main)
			main->CreateFiltersWindow(source);
	});

	connect(propertiesAction, &QAction::triggered, this, [this]() {
		OBSSource source = OBSGetStrongRef(weakSource);
		OBSBasic *main = OBSBasic::Get();
		if (source && main)
			main->CreatePropertiesWindow(source);
	});

	connect(hideAction, &QAction::triggered, this, [this]() {
		OBSSource source = OBSGetStrongRef(weakSource);
		if (!source)
			return;

		const bool hide = !hiddenInMixer;
		OBSDataAutoRelease priv = obs_source_get_private_settings(source);
		obs_data_set_bool(priv, "mixer_hidden", hide);
		hiddenInMixer = hide;

		/* Lets every mixer (ours and the stock one) re-evaluate visibility.
		 * Our own refresh is queued, never synchronous. */
		OBSBasic *main = OBSBasic::Get();
		if (main)
			emit main->mixerStatusChanged(sourceUuid);
	});

	menu->popup(menuButton->mapToGlobal(QPoint(0, menuButton->height())));
}

void VadocastMixerItem::obsFaderChanged(void *data, float)
{
	VadocastMixerItem *item = static_cast<VadocastMixerItem *>(data);
	QMetaObject::invokeMethod(item, "syncVolumeFromSource", Qt::QueuedConnection);
}

void VadocastMixerItem::obsSourceMuted(void *data, calldata_t *)
{
	VadocastMixerItem *item = static_cast<VadocastMixerItem *>(data);
	QMetaObject::invokeMethod(item, "syncMuteFromSource", Qt::QueuedConnection);
}

void VadocastMixerItem::obsVolmeterUpdated(void *data, const float[MAX_AUDIO_CHANNELS],
					   const float peak[MAX_AUDIO_CHANNELS], const float[MAX_AUDIO_CHANNELS])
{
	/* Audio thread: only touch the atomic */
	VadocastMixerItem *item = static_cast<VadocastMixerItem *>(data);

	float maxPeak = -100.0f;
	for (int i = 0; i < MAX_AUDIO_CHANNELS; i++) {
		if (std::isfinite(peak[i]) && peak[i] > maxPeak)
			maxPeak = peak[i];
	}

	float current = item->peakDb.load();
	while (maxPeak > current && !item->peakDb.compare_exchange_weak(current, maxPeak)) {
	}
}

/* ------------------------------------------------------------------------ */
/* VadocastMixer                                                             */
/* ------------------------------------------------------------------------ */

VadocastMixer::VadocastMixer(QWidget *parent) : QWidget(parent)
{
	setAttribute(Qt::WA_StyledBackground, true);
	setClasses(this, "vc-panel");

	QVBoxLayout *mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(12, 12, 12, 12);
	mainLayout->setSpacing(8);

	QLabel *title = new QLabel(QTStr("Vadocast.Mixer.Title"), this);
	setClasses(title, "vc-title");
	mainLayout->addWidget(title);

	scrollArea = new QScrollArea(this);
	scrollArea->setFrameShape(QFrame::NoFrame);
	scrollArea->setWidgetResizable(true);
	scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	scrollArea->viewport()->setAutoFillBackground(false);

	QWidget *content = new QWidget(scrollArea);
	content->setAutoFillBackground(false);
	QVBoxLayout *contentLayout = new QVBoxLayout(content);
	contentLayout->setContentsMargins(0, 0, 0, 0);
	contentLayout->setSpacing(0);

	static const char *sectionKeys[kSectionCount] = {"Vadocast.Mixer.Section.Microphones",
							 "Vadocast.Mixer.Section.Desktop",
							 "Vadocast.Mixer.Section.Media", "Vadocast.Mixer.Section.Other"};

	for (int i = 0; i < kSectionCount; i++) {
		sectionTitles[i] = new QLabel(QTStr(sectionKeys[i]), content);
		setClasses(sectionTitles[i], "vc-section-title");

		sectionContainers[i] = new QWidget(content);
		sectionLayouts[i] = new QVBoxLayout(sectionContainers[i]);
		sectionLayouts[i]->setContentsMargins(0, 0, 0, 0);
		sectionLayouts[i]->setSpacing(8);

		contentLayout->addWidget(sectionTitles[i]);
		contentLayout->addWidget(sectionContainers[i]);

		sectionTitles[i]->setVisible(false);
		sectionContainers[i]->setVisible(false);
	}

	emptyLabel = new QLabel(QTStr("Vadocast.Mixer.Empty"), content);
	emptyLabel->setWordWrap(true);
	emptyLabel->setAlignment(Qt::AlignCenter);
	setClasses(emptyLabel, "vc-caption");
	contentLayout->addSpacing(8);
	contentLayout->addWidget(emptyLabel);
	contentLayout->addStretch(1);

	scrollArea->setWidget(content);
	mainLayout->addWidget(scrollArea, 1);

	addButton = new QPushButton(QTStr("Vadocast.Mixer.AddAudio"), this);
	setClasses(addButton, "vc-secondary");

	hiddenButton = new QPushButton(this);
	hiddenButton->setCheckable(true);
	hiddenButton->setVisible(false);
	setClasses(hiddenButton, "vc-icon-button");

	QHBoxLayout *footer = new QHBoxLayout();
	footer->setContentsMargins(0, 0, 0, 0);
	footer->setSpacing(8);
	footer->addWidget(addButton);
	footer->addStretch(1);
	footer->addWidget(hiddenButton);
	mainLayout->addLayout(footer);

	connect(addButton, &QPushButton::clicked, this, &VadocastMixer::addAudioRequested);
	connect(hiddenButton, &QPushButton::toggled, this, [this](bool checked) {
		showHidden = checked;
		queueRefresh();
	});

	refreshTimer.setSingleShot(true);
	connect(&refreshTimer, &QTimer::timeout, this, &VadocastMixer::refresh);

	OBSBasic *main = OBSBasic::Get();
	if (main) {
		connect(main, &OBSBasic::mixerStatusChanged, this, [this](QString) { queueRefresh(); });
	}

	connectSignals();
	refresh();
}

VadocastMixer::~VadocastMixer()
{
	disconnectAll();
	clearItems();
}

void VadocastMixer::connectSignals()
{
	signal_handler_t *sh = obs_get_signal_handler();

	/* Same global hooks as the stock AudioMixer */
	static const char *const globalNames[] = {
		"source_create",   "source_remove",     "source_destroy",        "source_rename",
		"source_activate", "source_deactivate", "source_audio_activate", "source_audio_deactivate",
	};

	globalSignals.reserve(sizeof(globalNames) / sizeof(globalNames[0]));
	for (const char *name : globalNames)
		globalSignals.emplace_back(sh, name, VadocastMixer::obsGlobalSourceSignal, this);

	obs_frontend_add_event_callback(VadocastMixer::onFrontendEvent, this);
	frontendCallbackRegistered = true;

	updatePreviewHandlers();
}

void VadocastMixer::disconnectAll()
{
	globalSignals.clear();
	previewSignals.clear();

	if (frontendCallbackRegistered) {
		obs_frontend_remove_event_callback(VadocastMixer::onFrontendEvent, this);
		frontendCallbackRegistered = false;
	}
}

void VadocastMixer::clearItems()
{
	for (auto &entry : items) {
		if (entry.second)
			delete entry.second.data();
	}
	items.clear();
}

void VadocastMixer::updatePreviewHandlers()
{
	previewSignals.clear();

	if (!obs_frontend_preview_program_mode_active())
		return;

	OBSSourceAutoRelease previewSource = obs_frontend_get_current_preview_scene();
	if (!previewSource)
		return;

	previewSignals.emplace_back(obs_source_get_signal_handler(previewSource), "item_visible",
				    VadocastMixer::obsPreviewItemVisible, this);
}

void VadocastMixer::onFrontendEvent(enum obs_frontend_event event, void *data)
{
	VadocastMixer *mixer = static_cast<VadocastMixer *>(data);
	mixer->handleFrontendEvent((int)event);
}

void VadocastMixer::handleFrontendEvent(int event)
{
	switch ((enum obs_frontend_event)event) {
	case OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED:
		updatePreviewHandlers();
		queueRefresh();
		break;
	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		queueRefresh();
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		/* Release libobs objects before libobs shuts down */
		shuttingDown = true;
		refreshTimer.stop();
		disconnectAll();
		clearItems();
		break;
	default:
		break;
	}
}

void VadocastMixer::obsGlobalSourceSignal(void *data, calldata_t *params)
{
	obs_source_t *source = static_cast<obs_source_t *>(calldata_ptr(params, "source"));
	if (source && (obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO) == 0)
		return;

	QMetaObject::invokeMethod(static_cast<VadocastMixer *>(data), "queueRefresh", Qt::QueuedConnection);
}

void VadocastMixer::obsPreviewItemVisible(void *data, calldata_t *)
{
	QMetaObject::invokeMethod(static_cast<VadocastMixer *>(data), "queueRefresh", Qt::QueuedConnection);
}

void VadocastMixer::queueRefresh()
{
	if (shuttingDown)
		return;
	if (!refreshTimer.isActive())
		refreshTimer.start(0);
}

void VadocastMixer::refresh()
{
	if (shuttingDown)
		return;

	struct Entry {
		OBSSource source;
		QString uuid;
		QString name;
		VadocastMixerItem::Kind kind;
		int globalChannel;
		bool hidden;
		bool locked;
	};

	/* Global audio channels (Desktop Audio / Mic/Aux) */
	std::unordered_map<QString, int> globalChannels;
	for (int channel = kGlobalChannelFirst; channel <= kGlobalChannelLast; channel++) {
		OBSSourceAutoRelease source = obs_get_output_source(channel);
		QString uuid = sourceUuidOf(source);
		if (!uuid.isEmpty())
			globalChannels.emplace(uuid, channel);
	}

	/* Sources visible in the studio-mode preview scene count as shown, like the stock mixer */
	std::unordered_map<QString, bool> previewed;
	if (obs_frontend_preview_program_mode_active()) {
		OBSSourceAutoRelease previewSource = obs_frontend_get_current_preview_scene();
		obs_scene_t *previewScene = obs_scene_from_source(previewSource);
		if (previewScene) {
			auto collect = [](obs_scene_t *, obs_sceneitem_t *item, void *param) -> bool {
				auto *set = static_cast<std::unordered_map<QString, bool> *>(param);
				if (!obs_sceneitem_visible(item))
					return true;
				obs_source_t *source = obs_sceneitem_get_source(item);
				if (!source || (obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO) == 0)
					return true;
				if (!obs_source_audio_active(source))
					return true;
				QString uuid = sourceUuidOf(source);
				if (!uuid.isEmpty())
					set->emplace(uuid, true);
				return true;
			};
			obs_scene_enum_items(previewScene, collect, &previewed);
		}
	}

	/* Collect references first; obs_enum_sources holds the sources mutex */
	std::vector<OBSSource> candidates;
	auto enumProc = [](void *param, obs_source_t *source) -> bool {
		auto *list = static_cast<std::vector<OBSSource> *>(param);
		if ((obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO) != 0)
			list->emplace_back(source);
		return true;
	};
	obs_enum_sources(enumProc, &candidates);

	std::vector<Entry> entries;
	int hiddenCount = 0;

	for (OBSSource &source : candidates) {
		if (obs_source_removed(source))
			continue;

		QString uuid = sourceUuidOf(source);
		if (uuid.isEmpty())
			continue;

		bool already = std::any_of(entries.begin(), entries.end(),
					   [&uuid](const Entry &e) { return e.uuid == uuid; });
		if (already)
			continue;

		const bool audioActive = obs_source_audio_active(source);
		const bool active = obs_source_active(source) && audioActive;
		const bool isPreviewed = previewed.find(uuid) != previewed.end();
		auto globalIt = globalChannels.find(uuid);
		const int channel = globalIt != globalChannels.end() ? globalIt->second : 0;
		const bool pinned = isPinnedInMixer(source);
		const bool hidden = isHiddenInMixer(source);

		/* Stock mixer rule: pinned, audio-active (in program) or previewed */
		const bool relevant = pinned || active || isPreviewed || (channel != 0 && audioActive);
		if (!relevant)
			continue;

		if (hidden) {
			hiddenCount++;
			if (!showHidden)
				continue;
		}

		Entry entry{source,
			    uuid,
			    QString::fromUtf8(obs_source_get_name(source)),
			    classify(source, channel),
			    channel,
			    hidden,
			    isVolumeLocked(source)};
		entries.push_back(std::move(entry));
	}

	/* Global channels first (in channel order), then alphabetical */
	std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
		const bool ga = a.globalChannel != 0;
		const bool gb = b.globalChannel != 0;
		if (ga != gb)
			return ga;
		if (ga && gb && a.globalChannel != b.globalChannel)
			return a.globalChannel < b.globalChannel;
		return a.name.toLower() < b.name.toLower();
	});

	setUpdatesEnabled(false);

	/* Detach every card from the section layouts (widgets are kept) */
	for (int i = 0; i < kSectionCount; i++) {
		while (QLayoutItem *layoutItem = sectionLayouts[i]->takeAt(0))
			delete layoutItem;
	}

	/* Remove cards whose source is no longer shown */
	for (auto it = items.begin(); it != items.end();) {
		const QString &uuid = it->first;
		bool keep = std::any_of(entries.begin(), entries.end(),
					[&uuid](const Entry &e) { return e.uuid == uuid; });
		if (!keep || !it->second) {
			if (it->second)
				delete it->second.data();
			it = items.erase(it);
		} else {
			++it;
		}
	}

	int sectionCounts[kSectionCount] = {0, 0, 0, 0};

	for (const Entry &entry : entries) {
		const int section = sectionIndex(entry.kind);
		VadocastMixerItem *item = nullptr;

		auto it = items.find(entry.uuid);
		if (it != items.end() && it->second) {
			item = it->second;
			item->setKind(entry.kind);
			/* Only reparent when the section changes: reparenting hides the
			 * widget and would break an ongoing slider drag. */
			if (item->parentWidget() != sectionContainers[section])
				item->setParent(sectionContainers[section]);
		} else {
			item = new VadocastMixerItem(entry.source, entry.kind, sectionContainers[section]);
			items[entry.uuid] = item;
		}

		item->setDisplayName(entry.name);
		item->setHiddenInMixer(entry.hidden);
		item->setVolumeLocked(entry.locked);

		sectionLayouts[section]->addWidget(item);
		if (item->isHidden())
			item->show();
		sectionCounts[section]++;
	}

	for (int i = 0; i < kSectionCount; i++) {
		const bool visible = sectionCounts[i] > 0;
		sectionTitles[i]->setVisible(visible);
		sectionContainers[i]->setVisible(visible);
	}

	emptyLabel->setVisible(entries.empty());

	hiddenButton->setVisible(hiddenCount > 0 || showHidden);
	hiddenButton->setText(QTStr(showHidden ? "Vadocast.Mixer.HideHidden" : "Vadocast.Mixer.ShowHidden")
				      .arg(hiddenCount));

	setUpdatesEnabled(true);
}
