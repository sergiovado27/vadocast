/******************************************************************************
    vadocast - simple settings dialog (Vídeo, Audio, Teclas rápidas, General).
    Replaces the big OBS settings window. Writes the same profile / user
    config keys the original settings window uses, so the rest of the
    program keeps working unchanged.

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

#include "VadocastSettings.hpp"
#include "OBSHotkeyWidget.hpp"

#include <utility/VadocastGoLive.hpp>
#include <widgets/OBSBasic.hpp>

#include <OBSApp.hpp>
#include <qt-wrappers.hpp>
#include <util/dstr.h>
#include <vadocast-limits.h>

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSize>
#include <QStackedWidget>
#include <QTabBar>
#include <QVBoxLayout>

#include <cstring>
#include <map>

#include "moc_VadocastSettings.cpp"

extern bool restart;
extern bool EncoderAvailable(const char *encoder);
extern const char *get_simple_output_encoder(const char *encoder);

/* Global audio channels used by the original settings window. */
#define VC_DESKTOP_AUDIO_CHANNEL 1
#define VC_MIC_AUDIO_CHANNEL 3

#define VC_ENCODER_AUTO "auto"
#define VC_ENCODER_RAW_PREFIX "id:"

namespace {

/* ------------------------------------------------------------------------- */
/* Small UI helpers                                                          */

QLabel *MakeSectionTitle(const QString &text)
{
	QLabel *label = new QLabel(text);
	setClasses(label, "vc-section-title");
	return label;
}

QLabel *MakeCaption(const QString &text)
{
	QLabel *label = new QLabel(text);
	label->setWordWrap(true);
	setClasses(label, "vc-caption");
	return label;
}

QLabel *MakeLabel(const QString &text)
{
	QLabel *label = new QLabel(text);
	setClasses(label, "vc-label");
	return label;
}

QFrame *MakeCard(QFormLayout *&form)
{
	QFrame *card = new QFrame();
	setClasses(card, "vc-card");
	form = new QFormLayout(card);
	form->setContentsMargins(12, 10, 12, 10);
	form->setHorizontalSpacing(16);
	form->setVerticalSpacing(10);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	return card;
}

QComboBox *MakeCombo()
{
	QComboBox *combo = new QComboBox();
	setClasses(combo, "vc-input");
	combo->setMinimumWidth(260);
	return combo;
}

/* Page = scroll area with a title and a vertical list of cards. */
QWidget *MakePage(const QString &title, QVBoxLayout *&content)
{
	QScrollArea *scroll = new QScrollArea();
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->viewport()->setAutoFillBackground(false);

	QWidget *inner = new QWidget();
	inner->setAutoFillBackground(false);
	content = new QVBoxLayout(inner);
	content->setContentsMargins(16, 16, 16, 16);
	content->setSpacing(8);

	QLabel *titleLabel = new QLabel(title);
	setClasses(titleLabel, "vc-title");
	content->addWidget(titleLabel);

	scroll->setWidget(inner);
	return scroll;
}

void RememberLoadedIndex(QComboBox *combo)
{
	combo->setProperty("vcLoadedIndex", combo->currentIndex());
}

bool ComboChanged(QComboBox *combo)
{
	return combo && combo->isEnabled() && combo->currentIndex() != combo->property("vcLoadedIndex").toInt();
}

QString ResolutionLabel(uint32_t cx, uint32_t cy)
{
	const uint32_t shortSide = cx < cy ? cx : cy;
	return QString("%1p (%2x%3)").arg(shortSide).arg(cx).arg(cy);
}

int RecommendedBitrate(uint32_t shortSide, int fps)
{
	const bool high = fps > 30;
	if (shortSide <= 720) {
		return high ? 4500 : 3000;
	}
	if (shortSide <= 1080) {
		return high ? 6000 : 4500;
	}
	return high ? 13000 : 9000;
}

/* ------------------------------------------------------------------------- */
/* Encoders (same availability checks as the simple output mode)             */

struct EncoderOption {
	const char *labelKey;
	const char *simpleKey;
};

std::vector<EncoderOption> AvailableEncoders()
{
	/* Hardware first: this order is also the "Automático" preference. */
	std::vector<EncoderOption> list;
	if (EncoderAvailable("ffmpeg_nvenc") || EncoderAvailable("obs_nvenc_h264_tex")) {
		list.push_back({"Vadocast.Settings.Video.Encoder.NVENC", SIMPLE_ENCODER_NVENC});
	}
	if (EncoderAvailable("h264_texture_amf")) {
		list.push_back({"Vadocast.Settings.Video.Encoder.AMD", SIMPLE_ENCODER_AMD});
	}
	if (EncoderAvailable("obs_qsv11") || EncoderAvailable("obs_qsv11_v2")) {
		list.push_back({"Vadocast.Settings.Video.Encoder.QSV", SIMPLE_ENCODER_QSV});
	}
#ifdef __APPLE__
	if (EncoderAvailable("com.apple.videotoolbox.videoencoder.ave.avc")) {
		list.push_back({"Vadocast.Settings.Video.Encoder.Apple", SIMPLE_ENCODER_APPLE_H264});
	}
#endif
	list.push_back({"Vadocast.Settings.Video.Encoder.X264", SIMPLE_ENCODER_X264});
	return list;
}

const char *BestEncoderKey()
{
	std::vector<EncoderOption> list = AvailableEncoders();
	return list.empty() ? SIMPLE_ENCODER_X264 : list.front().simpleKey;
}

/* Maps an encoder type id (as used by the vertical output) to the simple
 * encoder key with the same hardware. nullptr when it is not one we list. */
const char *SimpleKeyFromEncoderId(const char *id)
{
	if (!id || !*id) {
		return nullptr;
	}
	const bool otherCodec = strstr(id, "hevc") || strstr(id, "av1") || strstr(id, "h265");
	if (strcmp(id, "obs_x264") == 0) {
		return SIMPLE_ENCODER_X264;
	}
	if (!otherCodec && strstr(id, "nvenc")) {
		return SIMPLE_ENCODER_NVENC;
	}
	if (!otherCodec && strstr(id, "qsv11")) {
		return SIMPLE_ENCODER_QSV;
	}
	if (strcmp(id, "h264_texture_amf") == 0 || strcmp(id, "amd_amf_h264") == 0) {
		return SIMPLE_ENCODER_AMD;
	}
	if (strcmp(id, "com.apple.videotoolbox.videoencoder.ave.avc") == 0) {
		return SIMPLE_ENCODER_APPLE_H264;
	}
	return nullptr;
}

void FillEncoderCombo(QComboBox *combo)
{
	combo->clear();
	combo->addItem(QTStr("Vadocast.Settings.Video.Encoder.Auto"), QString(VC_ENCODER_AUTO));
	for (const EncoderOption &opt : AvailableEncoders()) {
		combo->addItem(QTStr(opt.labelKey), QString(opt.simpleKey));
	}
}

/* Selects the entry for a simple encoder key. "Automático" is shown when the
 * key is the one the automatic choice would pick anyway. */
void SelectEncoderKey(QComboBox *combo, const QString &key, const QString &unknownLabel, const QString &unknownData)
{
	if (key.isEmpty() || key == QString(BestEncoderKey())) {
		combo->setCurrentIndex(0);
		return;
	}
	int idx = combo->findData(key);
	if (idx == -1) {
		combo->addItem(unknownLabel, unknownData);
		idx = combo->count() - 1;
	}
	combo->setCurrentIndex(idx);
}

QString EncoderDisplayName(const char *encoderId)
{
	const char *name = obs_encoder_get_display_name(encoderId);
	return QString::fromUtf8(name && *name ? name : encoderId);
}

/* Bitrate field: editable list with presets. */
QComboBox *MakeBitrateCombo()
{
	QComboBox *combo = MakeCombo();
	combo->setEditable(true);
	combo->setInsertPolicy(QComboBox::NoInsert);
	combo->setValidator(new QIntValidator(500, 100000, combo));
	const int presets[] = {2500, 3500, 4500, 6000, 8000};
	for (int preset : presets) {
		combo->addItem(QString::number(preset), preset);
	}
	return combo;
}

void SetBitrate(QComboBox *combo, int value)
{
	int idx = combo->findData(value);
	if (idx != -1) {
		combo->setCurrentIndex(idx);
	} else {
		combo->setEditText(QString::number(value));
	}
}

int GetBitrate(QComboBox *combo)
{
	bool ok = false;
	const int value = combo->currentText().trimmed().toInt(&ok);
	return ok && value >= 500 ? value : 0;
}

/* ------------------------------------------------------------------------- */
/* Audio devices (same mechanism as the original audio settings page)        */

void FillAudioDeviceCombo(QComboBox *combo, const char *sourceId, int channel)
{
	combo->clear();
	combo->addItem(QTStr("Vadocast.Settings.Audio.Disabled"), QString("disabled"));
	combo->addItem(QTStr("Vadocast.Settings.Audio.Default"), QString("default"));

	OBSProperties props = obs_get_source_properties(sourceId);
	if (props) {
		obs_property_t *list = obs_properties_get(props, "device_id");
		const size_t count = list ? obs_property_list_item_count(list) : 0;
		for (size_t i = 0; i < count; i++) {
			const char *name = obs_property_list_item_name(list, i);
			const char *val = obs_property_list_item_string(list, i);
			if (!val || !*val || strcmp(val, "default") == 0) {
				continue;
			}
			combo->addItem(QString::fromUtf8(name ? name : val), QString::fromUtf8(val));
		}
	}

	QString current = "disabled";
	OBSSourceAutoRelease source = obs_get_output_source(channel);
	if (source) {
		OBSDataAutoRelease settings = obs_source_get_settings(source);
		const char *deviceId = settings ? obs_data_get_string(settings, "device_id") : "";
		current = QString::fromUtf8(deviceId && *deviceId ? deviceId : "default");
	}

	int idx = combo->findData(current);
	if (idx == -1) {
		combo->addItem(QTStr("Vadocast.Settings.Audio.Unavailable"), current);
		idx = combo->count() - 1;
	}
	combo->setCurrentIndex(idx);
	RememberLoadedIndex(combo);
}

bool StreamIsLive(OBSBasic *main)
{
	bool live = false;
	if (VadocastGoLiveCall("vadocast_golive_active", &live)) {
		return live;
	}
	return main && main->StreamingActive();
}

} // namespace

/* ------------------------------------------------------------------------- */

VadocastSettings::VadocastSettings(QWidget *parent) : QDialog(parent), main(OBSBasic::Get())
{
	setWindowTitle(QTStr("Vadocast.Settings.Title"));
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
	setMinimumSize(760, 520);
	resize(860, 600);

	/* Same as the original settings window: no outputs start and no
	 * hotkeys fire while the dialog is open (key capture fields). */
	main->EnableOutputs(false);
	App()->DisableHotkeys();

	QVBoxLayout *root = new QVBoxLayout(this);
	root->setContentsMargins(0, 0, 0, 0);
	root->setSpacing(0);

	QHBoxLayout *body = new QHBoxLayout();
	body->setContentsMargins(0, 0, 0, 0);
	body->setSpacing(0);

	nav = new QListWidget();
	setClasses(nav, "vc-nav");
	nav->setFixedWidth(200);
	nav->setFrameShape(QFrame::NoFrame);
	nav->addItem(QTStr("Vadocast.Settings.Section.Video"));
	nav->addItem(QTStr("Vadocast.Settings.Section.Audio"));
	nav->addItem(QTStr("Vadocast.Settings.Section.Hotkeys"));
	nav->addItem(QTStr("Vadocast.Settings.Section.General"));

	pages = new QStackedWidget();
	pages->addWidget(BuildVideoPage());
	pages->addWidget(BuildAudioPage());
	pages->addWidget(BuildHotkeysPage());
	pages->addWidget(BuildGeneralPage());

	connect(nav, &QListWidget::currentRowChanged, pages, &QStackedWidget::setCurrentIndex);

	body->addWidget(nav);
	body->addWidget(pages, 1);
	root->addLayout(body, 1);

	QHBoxLayout *footer = new QHBoxLayout();
	footer->setContentsMargins(16, 12, 16, 16);
	footer->setSpacing(8);
	footer->addStretch(1);

	QPushButton *cancel = new QPushButton(QTStr("Vadocast.Settings.Cancel"));
	setClasses(cancel, "vc-secondary");
	QPushButton *save = new QPushButton(QTStr("Vadocast.Settings.Save"));
	setClasses(save, "vc-primary");
	save->setDefault(true);
	footer->addWidget(cancel);
	footer->addWidget(save);
	root->addLayout(footer);

	connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
	connect(save, &QPushButton::clicked, this, [this]() {
		Save();
		accept();
	});

	LoadVideo();
	LoadAudio();
	LoadGeneral();

	nav->setCurrentRow(0);
}

VadocastSettings::~VadocastSettings()
{
	main->EnableOutputs(true);
	App()->UpdateHotkeyFocusSetting();
}

void VadocastSettings::setSection(Section section)
{
	nav->setCurrentRow((int)section);
}

void VadocastSettings::openAt(QWidget *parent, Section section)
{
	VadocastSettings dlg(parent);
	dlg.setSection(section);
	dlg.exec();
}

/* ------------------------------------------------------------------------- */
/* Vídeo                                                                     */

QWidget *VadocastSettings::BuildVideoPage()
{
	QVBoxLayout *content = nullptr;
	QWidget *page = MakePage(QTStr("Vadocast.Settings.Section.Video"), content);

	videoLockedCaption = MakeCaption(QTStr("Vadocast.Settings.Video.Locked"));
	videoLockedCaption->setVisible(false);
	content->addWidget(videoLockedCaption);

	videoTabs = new QTabBar();
	setClasses(videoTabs, "vc-tabs");
	videoTabs->setDrawBase(false);
	videoTabs->setExpanding(false);
	videoTabs->addTab(QTStr("Vadocast.Settings.Video.Horizontal"));
	videoTabs->addTab(QTStr("Vadocast.Settings.Video.Vertical"));
	content->addWidget(videoTabs);

	videoTabPages = new QStackedWidget();
	connect(videoTabs, &QTabBar::currentChanged, videoTabPages, &QStackedWidget::setCurrentIndex);

	/* Horizontal ------------------------------------------------------- */
	QWidget *hPage = new QWidget();
	QVBoxLayout *hLayout = new QVBoxLayout(hPage);
	hLayout->setContentsMargins(0, 0, 0, 0);
	hLayout->setSpacing(8);

	QFormLayout *form = nullptr;
	hLayout->addWidget(MakeSectionTitle(QTStr("Vadocast.Settings.Video.Picture")));
	QFrame *card = MakeCard(form);
	hResolution = MakeCombo();
	hFps = MakeCombo();
	hFps->addItem(QTStr("Vadocast.Settings.Video.FpsValue").arg(30), 30);
	hFps->addItem(QTStr("Vadocast.Settings.Video.FpsValue").arg(60), 60);
	form->addRow(MakeLabel(QTStr("Vadocast.Settings.Video.Resolution")), hResolution);
	form->addRow(MakeLabel(QTStr("Vadocast.Settings.Video.Fps")), hFps);
	form->addRow(MakeCaption(QTStr("Vadocast.Settings.Video.ResolutionHint")));
	hLayout->addWidget(card);

	hLayout->addWidget(MakeSectionTitle(QTStr("Vadocast.Settings.Video.Quality")));
	card = MakeCard(form);
	hBitrate = MakeBitrateCombo();
	hEncoder = MakeCombo();
	hBitrateCaption = MakeCaption(QString());
	form->addRow(MakeLabel(QTStr("Vadocast.Settings.Video.Bitrate")), hBitrate);
	form->addRow(hBitrateCaption);
	form->addRow(MakeLabel(QTStr("Vadocast.Settings.Video.Encoder")), hEncoder);
	form->addRow(MakeCaption(QTStr("Vadocast.Settings.Video.EncoderHint")));
	hLayout->addWidget(card);
	hLayout->addStretch(1);

	/* Vertical --------------------------------------------------------- */
	QWidget *vPage = new QWidget();
	QVBoxLayout *vLayout = new QVBoxLayout(vPage);
	vLayout->setContentsMargins(0, 0, 0, 0);
	vLayout->setSpacing(8);

	vUnavailableCaption = MakeCaption(QTStr("Vadocast.Settings.Video.VerticalUnavailable"));
	vUnavailableCaption->setVisible(false);
	vLayout->addWidget(vUnavailableCaption);

	vLayout->addWidget(MakeSectionTitle(QTStr("Vadocast.Settings.Video.Picture")));
	card = MakeCard(form);
	vResolution = MakeCombo();
	form->addRow(MakeLabel(QTStr("Vadocast.Settings.Video.Resolution")), vResolution);
	form->addRow(MakeCaption(QTStr("Vadocast.Settings.Video.VerticalFps")));
	vLayout->addWidget(card);

	vLayout->addWidget(MakeSectionTitle(QTStr("Vadocast.Settings.Video.Quality")));
	card = MakeCard(form);
	vBitrate = MakeBitrateCombo();
	vEncoder = MakeCombo();
	vBitrateCaption = MakeCaption(QString());
	form->addRow(MakeLabel(QTStr("Vadocast.Settings.Video.Bitrate")), vBitrate);
	form->addRow(vBitrateCaption);
	form->addRow(MakeLabel(QTStr("Vadocast.Settings.Video.Encoder")), vEncoder);
	vLayout->addWidget(card);
	vLayout->addStretch(1);

	videoTabPages->addWidget(hPage);
	videoTabPages->addWidget(vPage);
	content->addWidget(videoTabPages, 1);

	auto update = [this]() { UpdateBitrateCaptions(); };
	connect(hResolution, &QComboBox::currentIndexChanged, this, update);
	connect(hFps, &QComboBox::currentIndexChanged, this, update);
	connect(vResolution, &QComboBox::currentIndexChanged, this, update);

	return page;
}

void VadocastSettings::LoadVideo()
{
	config_t *cfg = main->Config();

	/* Horizontal: output (scaled) resolution, never bigger than the canvas. */
	uint32_t baseCX = (uint32_t)config_get_uint(cfg, "Video", "BaseCX");
	uint32_t baseCY = (uint32_t)config_get_uint(cfg, "Video", "BaseCY");
	if (!vadocast_horizontal_allowed(baseCX, baseCY)) {
		baseCX = VADOCAST_DEFAULT_LONG_SIDE;
		baseCY = VADOCAST_DEFAULT_SHORT_SIDE;
	}
	uint32_t outCX = (uint32_t)config_get_uint(cfg, "Video", "OutputCX");
	uint32_t outCY = (uint32_t)config_get_uint(cfg, "Video", "OutputCY");
	if (!vadocast_horizontal_allowed(outCX, outCY) || outCX > baseCX || outCY > baseCY) {
		outCX = baseCX;
		outCY = baseCY;
	}

	hResolution->clear();
	const size_t hCount = VADOCAST_RESOLUTION_COUNT(vadocast_horizontal_resolutions);
	for (size_t i = hCount; i > 0; i--) {
		const vadocast_resolution &res = vadocast_horizontal_resolutions[i - 1];
		if (res.cx > baseCX || res.cy > baseCY) {
			continue;
		}
		hResolution->addItem(ResolutionLabel(res.cx, res.cy), QSize((int)res.cx, (int)res.cy));
		if (res.cx == outCX && res.cy == outCY) {
			hResolution->setCurrentIndex(hResolution->count() - 1);
		}
	}
	RememberLoadedIndex(hResolution);

	/* FPS: whatever the video currently runs at, rounded to 30 / 60. */
	int fps = 30;
	struct obs_video_info ovi;
	if (obs_get_video_info(&ovi) && ovi.fps_den) {
		fps = ((double)ovi.fps_num / (double)ovi.fps_den) > 45.0 ? 60 : 30;
	}
	hFps->setCurrentIndex(fps == 60 ? 1 : 0);
	RememberLoadedIndex(hFps);

	SetBitrate(hBitrate, (int)config_get_uint(cfg, "SimpleOutput", "VBitrate"));
	hBitrate->setProperty("vcLoadedBitrate", GetBitrate(hBitrate));

	FillEncoderCombo(hEncoder);
	const char *streamEnc = config_get_string(cfg, "SimpleOutput", "StreamEncoder");
	const QString encKey = QString::fromUtf8(streamEnc ? streamEnc : "");
	SelectEncoderKey(hEncoder, encKey == QString(SIMPLE_ENCODER_X264_LOWCPU) ? QString(SIMPLE_ENCODER_X264) : encKey,
			 EncoderDisplayName(get_simple_output_encoder(streamEnc ? streamEnc : SIMPLE_ENCODER_X264)),
			 encKey);
	RememberLoadedIndex(hEncoder);

	/* Vertical: through the vertical module's proc handlers. */
	proc_handler_t *ph = obs_get_proc_handler();
	calldata_t cd;

	vResolution->clear();
	calldata_init(&cd);
	verticalResAvailable = proc_handler_call(ph, "vadocast_vertical_get_resolution", &cd);
	if (verticalResAvailable) {
		loadedVCX = (uint32_t)calldata_int(&cd, "width");
		loadedVCY = (uint32_t)calldata_int(&cd, "height");
		const size_t vCount = VADOCAST_RESOLUTION_COUNT(vadocast_vertical_resolutions);
		for (size_t i = vCount; i > 0; i--) {
			const vadocast_resolution &res = vadocast_vertical_resolutions[i - 1];
			vResolution->addItem(ResolutionLabel(res.cx, res.cy), QSize((int)res.cx, (int)res.cy));
			if (res.cx == loadedVCX && res.cy == loadedVCY) {
				vResolution->setCurrentIndex(vResolution->count() - 1);
			}
		}
	}
	calldata_free(&cd);
	RememberLoadedIndex(vResolution);
	vResolution->setEnabled(verticalResAvailable);

	FillEncoderCombo(vEncoder);
	calldata_init(&cd);
	verticalEncAvailable = proc_handler_call(ph, "vadocast_vertical_get_encoder_settings", &cd);
	if (verticalEncAvailable) {
		loadedVBitrate = (int)calldata_int(&cd, "bitrate");
		const char *enc = calldata_string(&cd, "encoder");
		loadedVEncoder = enc ? enc : "";
	}
	calldata_free(&cd);

	if (verticalEncAvailable) {
		SetBitrate(vBitrate, loadedVBitrate > 0 ? loadedVBitrate : 6000);
		const char *key = SimpleKeyFromEncoderId(loadedVEncoder.c_str());
		if (key) {
			SelectEncoderKey(vEncoder, QString(key), EncoderDisplayName(loadedVEncoder.c_str()),
					 QString(VC_ENCODER_RAW_PREFIX) + QString::fromStdString(loadedVEncoder));
		} else if (loadedVEncoder.empty()) {
			vEncoder->setCurrentIndex(0);
		} else {
			vEncoder->addItem(EncoderDisplayName(loadedVEncoder.c_str()),
					  QString(VC_ENCODER_RAW_PREFIX) + QString::fromStdString(loadedVEncoder));
			vEncoder->setCurrentIndex(vEncoder->count() - 1);
		}
	}
	vBitrate->setProperty("vcLoadedBitrate", GetBitrate(vBitrate));
	RememberLoadedIndex(vEncoder);
	vBitrate->setEnabled(verticalEncAvailable);
	vEncoder->setEnabled(verticalEncAvailable);

	vUnavailableCaption->setVisible(!verticalResAvailable || !verticalEncAvailable);

	/* No video changes while live: the outputs are using these settings. */
	videoLocked = StreamIsLive(main);
	videoLockedCaption->setVisible(videoLocked);
	videoTabPages->setEnabled(!videoLocked);

	UpdateBitrateCaptions();
}

void VadocastSettings::UpdateBitrateCaptions()
{
	const int fps = hFps->currentData().toInt() > 0 ? hFps->currentData().toInt() : 30;
	const QString hint = QTStr("Vadocast.Settings.Video.BitrateHint");

	const QSize hSize = hResolution->currentData().toSize();
	if (hSize.isValid() && !hSize.isEmpty()) {
		const uint32_t shortSide = (uint32_t)qMin(hSize.width(), hSize.height());
		hBitrateCaption->setText(hint.arg(ResolutionLabel((uint32_t)hSize.width(), (uint32_t)hSize.height()))
						 .arg(fps)
						 .arg(RecommendedBitrate(shortSide, fps)));
	} else {
		hBitrateCaption->clear();
	}

	const QSize vSize = vResolution->currentData().toSize();
	if (vSize.isValid() && !vSize.isEmpty()) {
		const uint32_t shortSide = (uint32_t)qMin(vSize.width(), vSize.height());
		vBitrateCaption->setText(hint.arg(ResolutionLabel((uint32_t)vSize.width(), (uint32_t)vSize.height()))
						 .arg(fps)
						 .arg(RecommendedBitrate(shortSide, fps)));
	} else {
		vBitrateCaption->clear();
	}
}

void VadocastSettings::SaveVideo(bool &resetVideo, bool &resetOutputs)
{
	if (videoLocked) {
		return;
	}

	config_t *cfg = main->Config();

	/* vadocast always streams the main canvas with the simple output mode. */
	const char *mode = config_get_string(cfg, "Output", "Mode");
	if (!mode || astrcmpi(mode, "Simple") != 0) {
		config_set_string(cfg, "Output", "Mode", "Simple");
		resetOutputs = true;
	}

	if (ComboChanged(hResolution)) {
		const QSize size = hResolution->currentData().toSize();
		if (!size.isEmpty()) {
			config_set_uint(cfg, "Video", "OutputCX", (uint64_t)size.width());
			config_set_uint(cfg, "Video", "OutputCY", (uint64_t)size.height());
			resetVideo = true;
		}
	}

	if (ComboChanged(hFps)) {
		const int fps = hFps->currentData().toInt();
		config_set_uint(cfg, "Video", "FPSType", 0);
		config_set_string(cfg, "Video", "FPSCommon", fps == 60 ? "60" : "30");
		resetVideo = true;
	}

	const int bitrate = GetBitrate(hBitrate);
	if (bitrate > 0 && bitrate != hBitrate->property("vcLoadedBitrate").toInt()) {
		config_set_uint(cfg, "SimpleOutput", "VBitrate", (uint64_t)bitrate);
		resetOutputs = true;
	}

	if (ComboChanged(hEncoder)) {
		QString key = hEncoder->currentData().toString();
		if (key == QString(VC_ENCODER_AUTO)) {
			key = QString(BestEncoderKey());
		}
		config_set_string(cfg, "SimpleOutput", "StreamEncoder", QT_TO_UTF8(key));
		resetOutputs = true;
	}
}

void VadocastSettings::SaveVertical()
{
	if (videoLocked) {
		return;
	}

	proc_handler_t *ph = obs_get_proc_handler();
	calldata_t cd;
	bool failed = false;

	if (verticalResAvailable && ComboChanged(vResolution)) {
		const QSize size = vResolution->currentData().toSize();
		if (!size.isEmpty()) {
			calldata_init(&cd);
			calldata_set_int(&cd, "width", size.width());
			calldata_set_int(&cd, "height", size.height());
			const bool handled = proc_handler_call(ph, "vadocast_vertical_set_resolution", &cd);
			if (!handled || !calldata_bool(&cd, "success")) {
				failed = true;
			}
			calldata_free(&cd);
		}
	}

	if (verticalEncAvailable) {
		int bitrate = GetBitrate(vBitrate);
		const bool bitrateChanged = bitrate > 0 && bitrate != vBitrate->property("vcLoadedBitrate").toInt();
		const bool encoderChanged = ComboChanged(vEncoder);

		if (bitrateChanged || encoderChanged) {
			if (bitrate <= 0) {
				bitrate = loadedVBitrate > 0 ? loadedVBitrate : 6000;
			}

			std::string encoderId = loadedVEncoder;
			if (encoderChanged) {
				const QString data = vEncoder->currentData().toString();
				if (data.startsWith(QString(VC_ENCODER_RAW_PREFIX))) {
					encoderId = data.mid((int)strlen(VC_ENCODER_RAW_PREFIX)).toStdString();
				} else {
					const QString key = data == QString(VC_ENCODER_AUTO) ? QString(BestEncoderKey()) : data;
					encoderId = get_simple_output_encoder(QT_TO_UTF8(key));
				}
			}

			calldata_init(&cd);
			calldata_set_int(&cd, "bitrate", bitrate);
			calldata_set_string(&cd, "encoder", encoderId.c_str());
			const bool handled = proc_handler_call(ph, "vadocast_vertical_set_encoder_settings", &cd);
			if (!handled || !calldata_bool(&cd, "success")) {
				failed = true;
			}
			calldata_free(&cd);
		}
	}

	if (failed) {
		QMessageBox::warning(this, QTStr("Vadocast.Settings.Title"),
				     QTStr("Vadocast.Settings.Video.VerticalSaveFailed"));
	}
}

/* ------------------------------------------------------------------------- */
/* Audio                                                                     */

QWidget *VadocastSettings::BuildAudioPage()
{
	QVBoxLayout *content = nullptr;
	QWidget *page = MakePage(QTStr("Vadocast.Settings.Section.Audio"), content);

	QFormLayout *form = nullptr;
	content->addWidget(MakeSectionTitle(QTStr("Vadocast.Settings.Audio.Devices")));
	QFrame *card = MakeCard(form);
	micDevice = MakeCombo();
	desktopDevice = MakeCombo();
	form->addRow(MakeLabel(QTStr("Vadocast.Settings.Audio.Mic")), micDevice);
	form->addRow(MakeLabel(QTStr("Vadocast.Settings.Audio.Desktop")), desktopDevice);
	form->addRow(MakeCaption(QTStr("Vadocast.Settings.Audio.DevicesHint")));
	content->addWidget(card);

	content->addWidget(MakeSectionTitle(QTStr("Vadocast.Settings.Audio.QualityTitle")));
	card = MakeCard(form);
	audioBitrate = MakeCombo();
	const int bitrates[] = {96, 128, 160, 192};
	for (int br : bitrates) {
		audioBitrate->addItem(QTStr("Vadocast.Settings.Audio.BitrateValue").arg(br), br);
	}
	form->addRow(MakeLabel(QTStr("Vadocast.Settings.Audio.Quality")), audioBitrate);
	form->addRow(MakeCaption(QTStr("Vadocast.Settings.Audio.QualityHint")));
	content->addWidget(card);

	content->addStretch(1);
	return page;
}

void VadocastSettings::LoadAudio()
{
	FillAudioDeviceCombo(micDevice, App()->InputAudioSource(), VC_MIC_AUDIO_CHANNEL);
	FillAudioDeviceCombo(desktopDevice, App()->OutputAudioSource(), VC_DESKTOP_AUDIO_CHANNEL);

	const int current = (int)config_get_uint(main->Config(), "SimpleOutput", "ABitrate");
	int idx = audioBitrate->findData(current);
	if (idx == -1) {
		/* Not one of ours (e.g. 320 kbps): keep it unless the user picks another. */
		audioBitrate->addItem(QTStr("Vadocast.Settings.Audio.BitrateValue").arg(current), current);
		idx = audioBitrate->count() - 1;
	}
	audioBitrate->setCurrentIndex(idx);
	RememberLoadedIndex(audioBitrate);
}

void VadocastSettings::SaveAudio(bool &resetOutputs)
{
	if (ComboChanged(micDevice)) {
		main->ResetAudioDevice(App()->InputAudioSource(), QT_TO_UTF8(micDevice->currentData().toString()),
				       Str("Basic.AuxDevice1"), VC_MIC_AUDIO_CHANNEL);
	}
	if (ComboChanged(desktopDevice)) {
		main->ResetAudioDevice(App()->OutputAudioSource(),
				       QT_TO_UTF8(desktopDevice->currentData().toString()), Str("Basic.DesktopDevice1"),
				       VC_DESKTOP_AUDIO_CHANNEL);
	}
	if (ComboChanged(audioBitrate)) {
		config_set_uint(main->Config(), "SimpleOutput", "ABitrate",
				(uint64_t)audioBitrate->currentData().toInt());
		resetOutputs = true;
	}
}

/* ------------------------------------------------------------------------- */
/* Teclas rápidas                                                            */

namespace {

struct FoundHotkeys {
	obs_source_t *mic = nullptr;
	obs_hotkey_id startStream = OBS_INVALID_HOTKEY_ID;
	obs_hotkey_id stopStream = OBS_INVALID_HOTKEY_ID;
	obs_hotkey_id mute = OBS_INVALID_HOTKEY_ID;
	obs_hotkey_id unmute = OBS_INVALID_HOTKEY_ID;
	std::string startName;
	std::string stopName;
	std::string muteName;
	std::vector<std::pair<std::string, obs_hotkey_id>> scenes;
};

} // namespace

QWidget *VadocastSettings::BuildHotkeysPage()
{
	QVBoxLayout *content = nullptr;
	QWidget *page = MakePage(QTStr("Vadocast.Settings.Section.Hotkeys"), content);
	content->addWidget(MakeCaption(QTStr("Vadocast.Settings.Hotkeys.Hint")));

	/* Current bindings of every hotkey. */
	using keys_t = std::map<obs_hotkey_id, std::vector<obs_key_combination_t>>;
	keys_t keys;
	obs_enum_hotkey_bindings(
		[](void *data, size_t, obs_hotkey_binding_t *binding) {
			auto &keys = *static_cast<keys_t *>(data);
			keys[obs_hotkey_binding_get_hotkey_id(binding)].emplace_back(
				obs_hotkey_binding_get_key_combination(binding));
			return true;
		},
		&keys);

	/* Find the few hotkeys vadocast shows. */
	OBSSourceAutoRelease mic = obs_get_output_source(VC_MIC_AUDIO_CHANNEL);
	FoundHotkeys found;
	found.mic = mic.Get();

	obs_enum_hotkeys(
		[](void *data, obs_hotkey_id id, obs_hotkey_t *key) {
			FoundHotkeys &f = *static_cast<FoundHotkeys *>(data);
			const char *name = obs_hotkey_get_name(key);
			if (!name) {
				return true;
			}

			switch (obs_hotkey_get_registerer_type(key)) {
			case OBS_HOTKEY_REGISTERER_FRONTEND:
				if (strcmp(name, "OBSBasic.StartStreaming") == 0) {
					f.startStream = id;
					f.startName = name;
				} else if (strcmp(name, "OBSBasic.StopStreaming") == 0) {
					f.stopStream = id;
					f.stopName = name;
				}
				break;

			case OBS_HOTKEY_REGISTERER_SOURCE: {
				auto weak = static_cast<obs_weak_source_t *>(obs_hotkey_get_registerer(key));
				if (!weak) {
					break;
				}
				if (strcmp(name, "OBSBasic.SelectScene") == 0) {
					OBSSource source = OBSGetStrongRef(weak);
					if (source && obs_scene_from_source(source)) {
						const char *sceneName = obs_source_get_name(source);
						f.scenes.emplace_back(sceneName ? sceneName : "", id);
					}
				} else if (f.mic && obs_weak_source_references_source(weak, f.mic)) {
					if (strcmp(name, "libobs.mute") == 0) {
						f.mute = id;
						f.muteName = name;
					} else if (strcmp(name, "libobs.unmute") == 0) {
						f.unmute = id;
					}
				}
				break;
			}

			default:
				break;
			}
			return true;
		},
		&found);

	auto addRow = [&](QFormLayout *form, const QString &label, obs_hotkey_id id, const std::string &name,
			  bool frontend, obs_hotkey_id mirrorId) {
		if (id == OBS_INVALID_HOTKEY_ID) {
			return;
		}
		QWidget *parentCard = form->parentWidget();
		auto it = keys.find(id);
		OBSHotkeyWidget *hw = nullptr;
		if (it == keys.end()) {
			hw = new OBSHotkeyWidget(parentCard, id, name, nullptr);
		} else {
			hw = new OBSHotkeyWidget(parentCard, id, name, nullptr, it->second);
		}
		hw->setAccessibleName(label);
		form->addRow(MakeLabel(label), hw);

		HotkeyEntry entry;
		entry.widget = hw;
		entry.id = id;
		entry.mirrorId = mirrorId;
		entry.frontend = frontend;
		entry.name = name;
		hotkeyEntries.push_back(entry);
	};

	QFormLayout *form = nullptr;

	content->addWidget(MakeSectionTitle(QTStr("Vadocast.Settings.Hotkeys.Stream")));
	QFrame *card = MakeCard(form);
	addRow(form, QTStr("Vadocast.Settings.Hotkeys.StartStream"), found.startStream, found.startName, true,
	       OBS_INVALID_HOTKEY_ID);
	addRow(form, QTStr("Vadocast.Settings.Hotkeys.StopStream"), found.stopStream, found.stopName, true,
	       OBS_INVALID_HOTKEY_ID);
	/* Same key for mute and unmute: libobs treats the pair as a toggle. */
	addRow(form, QTStr("Vadocast.Settings.Hotkeys.ToggleMic"), found.mute, found.muteName, false, found.unmute);
	if (found.mute == OBS_INVALID_HOTKEY_ID) {
		form->addRow(MakeCaption(QTStr("Vadocast.Settings.Hotkeys.NoMic")));
	}
	content->addWidget(card);

	content->addWidget(MakeSectionTitle(QTStr("Vadocast.Settings.Hotkeys.Scenes")));
	card = MakeCard(form);
	for (const auto &scene : found.scenes) {
		addRow(form, QTStr("Vadocast.Settings.Hotkeys.SwitchScene").arg(QString::fromStdString(scene.first)),
		       scene.second, "OBSBasic.SelectScene", false, OBS_INVALID_HOTKEY_ID);
	}
	if (found.scenes.empty()) {
		form->addRow(MakeCaption(QTStr("Vadocast.Settings.Hotkeys.NoScenes")));
	}
	content->addWidget(card);

	content->addStretch(1);
	return page;
}

void VadocastSettings::SaveHotkeys()
{
	config_t *cfg = main->Config();

	for (HotkeyEntry &entry : hotkeyEntries) {
		if (!entry.widget || !entry.widget->Changed()) {
			continue;
		}

		std::vector<obs_key_combination_t> combinations;
		entry.widget->Save(combinations);

		if (entry.mirrorId != OBS_INVALID_HOTKEY_ID) {
			obs_hotkey_load_bindings(entry.mirrorId, combinations.data(), combinations.size());
		}

		/* Frontend hotkeys live in the profile; source (scene / mic) hotkeys
		 * are saved with the scene collection (SaveProject). */
		if (entry.frontend) {
			OBSDataArrayAutoRelease array = obs_hotkey_save(entry.id);
			OBSDataAutoRelease data = obs_data_create();
			obs_data_set_array(data, "bindings", array);
			config_set_string(cfg, "Hotkeys", entry.name.c_str(), obs_data_get_json(data));
		}
	}
}

/* ------------------------------------------------------------------------- */
/* General                                                                   */

QWidget *VadocastSettings::BuildGeneralPage()
{
	QVBoxLayout *content = nullptr;
	QWidget *page = MakePage(QTStr("Vadocast.Settings.Section.General"), content);

	QFormLayout *form = nullptr;
	content->addWidget(MakeSectionTitle(QTStr("Vadocast.Settings.General.LanguageTitle")));
	QFrame *card = MakeCard(form);
	language = MakeCombo();
	/* Language names are shown in their own language. */
	language->addItem(QString::fromUtf8("Español"), QString("es-ES"));
	language->addItem(QString("English"), QString("en-US"));
	form->addRow(MakeLabel(QTStr("Vadocast.Settings.General.Language")), language);
	form->addRow(MakeCaption(QTStr("Vadocast.Settings.General.LanguageHint")));
	content->addWidget(card);

	content->addWidget(MakeSectionTitle(QTStr("Vadocast.Settings.General.StreamTitle")));
	card = MakeCard(form);
	warnBeforeStart = new QCheckBox(QTStr("Vadocast.Settings.General.WarnBeforeStart"));
	warnBeforeStop = new QCheckBox(QTStr("Vadocast.Settings.General.WarnBeforeStop"));
	form->addRow(warnBeforeStart);
	form->addRow(warnBeforeStop);
	content->addWidget(card);

	content->addWidget(MakeSectionTitle(QTStr("Vadocast.Settings.General.WindowTitle")));
	card = MakeCard(form);
	minimizeToTray = new QCheckBox(QTStr("Vadocast.Settings.General.MinimizeToTray"));
	form->addRow(minimizeToTray);
	content->addWidget(card);

	content->addStretch(1);
	return page;
}

void VadocastSettings::LoadGeneral()
{
	const char *locale = App()->GetLocale();
	const bool spanish = locale && strncmp(locale, "es", 2) == 0;
	language->setCurrentIndex(spanish ? 0 : 1);
	RememberLoadedIndex(language);

	config_t *user = App()->GetUserConfig();
	warnBeforeStart->setChecked(config_get_bool(user, "BasicWindow", "WarnBeforeStartingStream"));
	warnBeforeStop->setChecked(config_get_bool(user, "BasicWindow", "WarnBeforeStoppingStream"));
	minimizeToTray->setChecked(config_get_bool(user, "BasicWindow", "SysTrayMinimizeToTray"));
}

void VadocastSettings::SaveGeneral()
{
	config_t *user = App()->GetUserConfig();

	const bool langChanged = ComboChanged(language);
	if (langChanged) {
		config_set_string(user, "General", "Language", QT_TO_UTF8(language->currentData().toString()));
	}
	/* Same flag the original settings window sets: the main window offers
	 * to restart after the dialog closes. */
	restart = langChanged;

	config_set_bool(user, "BasicWindow", "WarnBeforeStartingStream", warnBeforeStart->isChecked());
	config_set_bool(user, "BasicWindow", "WarnBeforeStoppingStream", warnBeforeStop->isChecked());

	const bool toTray = minimizeToTray->isChecked();
	if (toTray != config_get_bool(user, "BasicWindow", "SysTrayMinimizeToTray")) {
		config_set_bool(user, "BasicWindow", "SysTrayMinimizeToTray", toTray);
	}
	/* Minimizing to the tray needs the tray icon. */
	if (toTray && !config_get_bool(user, "BasicWindow", "SysTrayEnabled")) {
		config_set_bool(user, "BasicWindow", "SysTrayEnabled", true);
		main->SystemTray(false);
	}
}

/* ------------------------------------------------------------------------- */

void VadocastSettings::Save()
{
	bool resetVideo = false;
	bool resetOutputs = false;

	SaveVideo(resetVideo, resetOutputs);
	SaveAudio(resetOutputs);
	SaveHotkeys();
	SaveGeneral();

	config_save_safe(main->Config(), "tmp", nullptr);
	config_save_safe(App()->GetUserConfig(), "tmp", nullptr);

	if (resetVideo) {
		if (main->ResetVideo() != OBS_VIDEO_SUCCESS) {
			QMessageBox::warning(this, QTStr("Vadocast.Settings.Title"),
					     QTStr("Vadocast.Settings.Video.ResetFailed"));
		}
	}
	if (resetVideo || resetOutputs) {
		main->ResetOutputs();
	}

	/* The vertical output follows the main FPS, so it goes after ResetVideo. */
	SaveVertical();

	main->SaveProject();
	blog(LOG_INFO, "[vadocast] settings saved");
}
