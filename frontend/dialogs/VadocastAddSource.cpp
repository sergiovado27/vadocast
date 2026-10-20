/******************************************************************************
    vadocast: all-in-one "add source" / "edit source" panel.

    Based on the source creation of OBSBasicSourceSelect and the preview /
    properties code of OBSBasicProperties.

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

#include "VadocastAddSource.hpp"

#include "OBSApp.hpp"

#include <utility/display-helpers.hpp>
#include <widgets/OBSBasic.hpp>
#include <widgets/OBSQTDisplay.hpp>

#include <properties-view.hpp>
#include <qt-wrappers.hpp>

#include <QCheckBox>
#include <QDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#include <Windows.h>
#endif

namespace {

/* ------------------------------------------------------------------------- */
/* Source types offered by the panel                                         */

struct VadocastSourceType {
	const char *key; /* Vadocast.AddSource.Type.<key>[.Caption|.Default] */
	std::vector<const char *> candidates; /* unversioned ids, first registered one wins */
	bool fillCanvas; /* fit to the whole canvas (captures, cameras) or centered at natural size */
};

const std::vector<VadocastSourceType> &SourceTypes()
{
	static const std::vector<VadocastSourceType> types = {
		{"Screen",
		 {"monitor_capture", "screen_capture", "pipewire-screen-capture-source",
		  "pipewire-desktop-capture-source", "xshm_input_v2", "xshm_input", "display_capture"},
		 true},
		{"Window", {"window_capture", "pipewire-window-capture-source", "xcomposite_input"}, true},
		{"Game", {"game_capture"}, true},
		{"Camera", {"dshow_input", "macos-avcapture", "av_capture_input", "v4l2_input", "pipewire-camera-source"}, true},
		{"Image", {"image_source"}, false},
		{"Media", {"ffmpeg_source"}, false},
#ifdef _WIN32
		{"Text", {"text_gdiplus", "text_ft2_source"}, false},
#else
		{"Text", {"text_ft2_source"}, false},
#endif
		{"Browser", {"browser_source"}, false},
	};
	return types;
}

/* Essential settings per (unversioned) source type id. Everything else is hidden. */
const std::unordered_map<std::string, std::vector<std::string>> &Whitelists()
{
	static const std::unordered_map<std::string, std::vector<std::string>> lists = {
		{"dshow_input",
		 {"video_device_id", "res_type", "resolution", "frame_interval", "video_format", "color_space",
		  "color_range", "audio_output_mode", "use_custom_audio_device", "audio_device_id"}},
		{"monitor_capture", {"monitor_id", "monitor", "capture_cursor"}},
		{"window_capture", {"window", "cursor", "capture_audio"}},
		{"game_capture", {"capture_mode", "window", "capture_cursor", "capture_audio"}},
		{"image_source", {"file"}},
		{"ffmpeg_source", {"local_file", "looping", "restart_on_activate"}},
		{"text_gdiplus", {"text", "font", "color", "outline", "outline_size", "outline_color"}},
		{"text_ft2_source", {"text", "font", "color1", "outline", "drop_shadow"}},
		{"browser_source", {"url", "width", "height"}},
	};
	return lists;
}

const std::vector<std::string> *WhitelistFor(obs_source_t *source)
{
	const char *id = source ? obs_source_get_unversioned_id(source) : nullptr;
	if (!id) {
		return nullptr;
	}
	auto it = Whitelists().find(id);
	return it != Whitelists().end() ? &it->second : nullptr;
}

/* Latest registered (and usable) input id for one of the panel types, or empty. */
std::string ResolveTypeId(const VadocastSourceType &type)
{
	for (const char *candidate : type.candidates) {
		size_t idx = 0;
		const char *id = nullptr;
		const char *unversioned = nullptr;
		while (obs_enum_input_types2(idx++, &id, &unversioned)) {
			if (!id || !unversioned) {
				continue;
			}
			if (strcmp(unversioned, candidate) != 0 && strcmp(id, candidate) != 0) {
				continue;
			}
			const char *latest = obs_get_latest_input_type_id(unversioned);
			if (!latest) {
				latest = id;
			}
			const uint32_t flags = obs_get_source_output_flags(latest);
			if ((flags & (OBS_SOURCE_CAP_DISABLED | OBS_SOURCE_DEPRECATED)) != 0) {
				continue;
			}
			return latest;
		}
	}
	return std::string();
}

std::string UniqueSourceName(const std::string &base, obs_source_t *self)
{
	std::string name = base;
	int suffix = 1;
	for (;;) {
		OBSSourceAutoRelease existing = obs_get_source_by_name(name.c_str());
		if (!existing || existing.Get() == self) {
			return name;
		}
		name = base + " " + std::to_string(++suffix);
	}
}

/* ------------------------------------------------------------------------- */
/* Property filtering                                                        */

struct PropertyFilter {
	const std::vector<std::string> *whitelist = nullptr;
	obs_properties_t *last = nullptr; /* properties currently shown by the view */
};

/* Keyed by source: OBSPropertiesView calls the reload callback with the source only. UI thread only. */
std::unordered_map<obs_source_t *, PropertyFilter> &PropertyFilters()
{
	static std::unordered_map<obs_source_t *, PropertyFilter> filters;
	return filters;
}

/* Hides every property that is not in the whitelist. Returns true if something stays visible. */
bool HideNotListed(obs_properties_t *props, const std::vector<std::string> &whitelist)
{
	bool anyVisible = false;
	obs_property_t *p = obs_properties_first(props);
	while (p) {
		const char *name = obs_property_name(p);
		const bool listed = name && std::find(whitelist.begin(), whitelist.end(), std::string(name)) !=
						    whitelist.end();
		if (obs_property_get_type(p) == OBS_PROPERTY_GROUP) {
			obs_properties_t *content = obs_property_group_content(p);
			const bool childVisible = content ? HideNotListed(content, whitelist) : false;
			if (!listed && !childVisible) {
				obs_property_set_visible(p, false);
			}
		} else if (!listed) {
			obs_property_set_visible(p, false);
		}
		if (obs_property_visible(p)) {
			anyVisible = true;
		}
		obs_property_next(&p);
	}
	return anyVisible;
}

obs_properties_t *FilteredSourceProperties(void *obj)
{
	obs_source_t *source = static_cast<obs_source_t *>(obj);
	obs_properties_t *props = obs_source_properties(source);
	auto it = PropertyFilters().find(source);
	if (props && it != PropertyFilters().end()) {
		it->second.last = props;
		if (it->second.whitelist) {
			HideNotListed(props, *it->second.whitelist);
		}
	}
	return props;
}

bool VerticalCanvasAvailable()
{
	proc_handler_t *ph = obs_get_proc_handler();
	if (!ph) {
		return false;
	}
	calldata_t cd;
	calldata_init(&cd);
	const bool ok = proc_handler_call(ph, "vadocast_vertical_get_resolution", &cd) && calldata_int(&cd, "width") > 0;
	calldata_free(&cd);
	return ok;
}

/* ------------------------------------------------------------------------- */
/* Dialog                                                                    */

class VadocastSourceDialog : public QDialog {
public:
	VadocastSourceDialog(QWidget *parent, obs_scene_t *scene_);
	VadocastSourceDialog(QWidget *parent, obs_source_t *source_);
	~VadocastSourceDialog() override;

	void reject() override;

protected:
	bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;

private:
	const bool addMode;
	OBSScene scene;

	std::mutex sourceMutex; /* guards `source` for the preview draw callback */
	OBSSource source;
	bool showing = false;
	OBSData oldSettings;
	const VadocastSourceType *currentType = nullptr;

	OBSSignal updatePropertiesSignal;
	OBSSignal removedSignal;

	QListWidget *nav = nullptr;
	QWidget *placeholder = nullptr;
	QWidget *body = nullptr;
	QLabel *titleLabel = nullptr;
	QLineEdit *nameEdit = nullptr;
	QFrame *previewFrame = nullptr;
	OBSQTDisplay *preview = nullptr;
	QFrame *settingsCard = nullptr;
	QVBoxLayout *settingsLayout = nullptr;
	QPointer<OBSPropertiesView> view;
	QCheckBox *verticalCheck = nullptr;
	QPushButton *primaryButton = nullptr;
	QTimer *deferTimer = nullptr;
	std::vector<const VadocastSourceType *> navTypes;
	bool drawCallbackAdded = false;
	bool verticalAvailable = false;

	void BuildUi();
	void SelectType(int row);
	void SetupSource(obs_source_t *src);
	void ClearSource(bool removeSource);
	void Refilter();
	void AddClicked();
	void SaveClicked();
	void Cleanup();

	static void DrawPreview(void *data, uint32_t cx, uint32_t cy);
	static void UpdateProperties(void *data, calldata_t *params);
	static void SourceRemoved(void *data, calldata_t *params);
};

VadocastSourceDialog::VadocastSourceDialog(QWidget *parent, obs_scene_t *scene_)
	: QDialog(parent),
	  addMode(true),
	  scene(scene_)
{
	BuildUi();
	setWindowTitle(QTStr("Vadocast.AddSource.Title"));
	resize(720, 620);
	App()->DisableHotkeys();
}

VadocastSourceDialog::VadocastSourceDialog(QWidget *parent, obs_source_t *source_)
	: QDialog(parent),
	  addMode(false),
	  oldSettings(obs_data_create())
{
	/* The OBSData constructor increments the reference once */
	obs_data_release(oldSettings);
	OBSDataAutoRelease current = obs_source_get_settings(source_);
	obs_data_apply(oldSettings, current);

	BuildUi();
	setWindowTitle(QTStr("Vadocast.AddSource.EditTitle"));
	resize(WhitelistFor(source_) ? 560 : 640, 620);

	removedSignal.Connect(obs_source_get_signal_handler(source_), "remove", VadocastSourceDialog::SourceRemoved,
			      this);
	SetupSource(source_);
	App()->DisableHotkeys();
}

VadocastSourceDialog::~VadocastSourceDialog()
{
	Cleanup();
	/* Closed without going through accept/reject (e.g. the parent was destroyed) */
	ClearSource(addMode);
	App()->UpdateHotkeyFocusSetting();
}

void VadocastSourceDialog::BuildUi()
{
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

	auto root = new QVBoxLayout(this);
	root->setContentsMargins(16, 16, 16, 16);
	root->setSpacing(12);

	auto mainRow = new QHBoxLayout();
	mainRow->setSpacing(12);
	root->addLayout(mainRow, 1);

	/* Left navigation with the source types (add mode only) */
	if (addMode) {
		nav = new QListWidget(this);
		setClasses(nav, "vc-nav");
		nav->setFixedWidth(220);
		nav->setSelectionMode(QAbstractItemView::SingleSelection);
		nav->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

		OBSBasic *main = OBSBasic::Get();
		for (const auto &type : SourceTypes()) {
			const std::string id = ResolveTypeId(type);
			if (id.empty()) {
				continue;
			}
			const std::string base = std::string("Vadocast.AddSource.Type.") + type.key;

			auto item = new QListWidgetItem(nav);
			auto row = new QWidget();
			row->setAttribute(Qt::WA_TransparentForMouseEvents);
			auto rowLayout = new QHBoxLayout(row);
			rowLayout->setContentsMargins(8, 6, 8, 6);
			rowLayout->setSpacing(10);

			auto icon = new QLabel(row);
			if (main) {
				icon->setPixmap(main->GetSourceIcon(id.c_str()).pixmap(20, 20));
			}
			icon->setFixedSize(20, 20);
			rowLayout->addWidget(icon, 0, Qt::AlignTop);

			auto texts = new QVBoxLayout();
			texts->setContentsMargins(0, 0, 0, 0);
			texts->setSpacing(2);
			auto name = new QLabel(QTStr(base.c_str()), row);
			setClasses(name, "vc-label");
			auto caption = new QLabel(QTStr((base + ".Caption").c_str()), row);
			setClasses(caption, "vc-caption");
			caption->setWordWrap(true);
			texts->addWidget(name);
			texts->addWidget(caption);
			rowLayout->addLayout(texts, 1);

			item->setSizeHint(QSize(0, std::max(56, row->sizeHint().height())));
			nav->setItemWidget(item, row);
			navTypes.push_back(&type);
		}
		connect(nav, &QListWidget::currentRowChanged, this, [this](int row) { SelectType(row); });
		mainRow->addWidget(nav);
	}

	auto content = new QWidget(this);
	auto contentLayout = new QVBoxLayout(content);
	contentLayout->setContentsMargins(0, 0, 0, 0);
	contentLayout->setSpacing(0);
	mainRow->addWidget(content, 1);

	/* Shown until a type is picked */
	if (addMode) {
		placeholder = new QWidget(content);
		auto pl = new QVBoxLayout(placeholder);
		pl->addStretch(1);
		auto title = new QLabel(QTStr("Vadocast.AddSource.ChooseType"), placeholder);
		setClasses(title, "vc-title");
		title->setAlignment(Qt::AlignCenter);
		auto hint = new QLabel(QTStr("Vadocast.AddSource.ChooseTypeHint"), placeholder);
		setClasses(hint, "vc-caption");
		hint->setAlignment(Qt::AlignCenter);
		hint->setWordWrap(true);
		pl->addWidget(title);
		pl->addWidget(hint);
		pl->addStretch(1);
		contentLayout->addWidget(placeholder, 1);
	}

	body = new QWidget(content);
	auto bodyLayout = new QVBoxLayout(body);
	bodyLayout->setContentsMargins(0, 0, 0, 0);
	bodyLayout->setSpacing(8);
	contentLayout->addWidget(body, 1);

	titleLabel = new QLabel(body);
	setClasses(titleLabel, "vc-title");
	bodyLayout->addWidget(titleLabel);

	auto nameTitle = new QLabel(QTStr("Vadocast.AddSource.Name"), body);
	setClasses(nameTitle, "vc-section-title");
	bodyLayout->addWidget(nameTitle);

	nameEdit = new QLineEdit(body);
	setClasses(nameEdit, "vc-input");
	bodyLayout->addWidget(nameEdit);

	/* Live preview of the source alone, scaled to fit (same drawing as the properties window) */
	previewFrame = new QFrame(body);
	setClasses(previewFrame, "vc-preview");
	previewFrame->setFixedHeight(220);
	auto previewLayout = new QVBoxLayout(previewFrame);
	previewLayout->setContentsMargins(0, 0, 0, 0);
	preview = new OBSQTDisplay(previewFrame);
	preview->SetDisplayBackgroundColor(QColor(0x0B, 0x0B, 0x0B));
	previewLayout->addWidget(preview);
	bodyLayout->addWidget(previewFrame);

	connect(preview, &OBSQTDisplay::DisplayCreated, this, [this]() {
		if (!drawCallbackAdded && preview->GetDisplay()) {
			obs_display_add_draw_callback(preview->GetDisplay(), VadocastSourceDialog::DrawPreview, this);
			drawCallbackAdded = true;
		}
	});

	auto settingsTitle = new QLabel(QTStr("Vadocast.AddSource.Settings"), body);
	setClasses(settingsTitle, "vc-section-title");
	bodyLayout->addWidget(settingsTitle);

	settingsCard = new QFrame(body);
	setClasses(settingsCard, "vc-card");
	settingsLayout = new QVBoxLayout(settingsCard);
	settingsLayout->setContentsMargins(8, 8, 8, 8);
	bodyLayout->addWidget(settingsCard, 1);

	if (addMode) {
		verticalCheck = new QCheckBox(QTStr("Vadocast.AddSource.AddToVertical"), body);
		verticalCheck->setChecked(true);
		verticalAvailable = VerticalCanvasAvailable();
		verticalCheck->setVisible(verticalAvailable);
		bodyLayout->addWidget(verticalCheck);
	}

	/* Footer: actions on the right, primary action last */
	auto footer = new QHBoxLayout();
	footer->setSpacing(8);
	footer->addStretch(1);
	auto cancelButton = new QPushButton(QTStr("Vadocast.AddSource.Cancel"), this);
	setClasses(cancelButton, "vc-secondary");
	cancelButton->setAutoDefault(false);
	primaryButton =
		new QPushButton(QTStr(addMode ? "Vadocast.AddSource.Add" : "Vadocast.AddSource.Save"), this);
	setClasses(primaryButton, "vc-primary");
	primaryButton->setAutoDefault(false);
	footer->addWidget(cancelButton);
	footer->addWidget(primaryButton);
	root->addLayout(footer);

	connect(cancelButton, &QPushButton::clicked, this, [this]() { reject(); });
	connect(primaryButton, &QPushButton::clicked, this, [this]() {
		if (addMode) {
			AddClicked();
		} else {
			SaveClicked();
		}
	});

	/* Deferred properties (e.g. web pages) still refresh the preview, a moment after the last change */
	deferTimer = new QTimer(this);
	deferTimer->setSingleShot(true);
	deferTimer->setInterval(700);
	connect(deferTimer, &QTimer::timeout, this, [this]() {
		if (view && view->DeferUpdate()) {
			view->UpdateSettings();
		}
	});

	if (addMode) {
		body->setVisible(false);
		primaryButton->setEnabled(false);
	}
}

void VadocastSourceDialog::SelectType(int row)
{
	if (row < 0 || row >= (int)navTypes.size()) {
		return;
	}
	const VadocastSourceType *type = navTypes[row];
	if (type == currentType && source) {
		return;
	}

	ClearSource(true);
	currentType = type;

	const std::string id = ResolveTypeId(*type);
	const std::string base = std::string("Vadocast.AddSource.Type.") + type->key;
	const std::string defaultName = QT_TO_UTF8(QTStr((base + ".Default").c_str()));
	const std::string name = UniqueSourceName(defaultName, nullptr);

	OBSSourceAutoRelease created = id.empty() ? nullptr : obs_source_create(id.c_str(), name.c_str(), nullptr, nullptr);
	if (!created) {
		if (placeholder) {
			placeholder->setVisible(true);
		}
		body->setVisible(false);
		primaryButton->setEnabled(false);
		OBSMessageBox::warning(this, QTStr("Vadocast.AddSource.Title"), QTStr("Vadocast.AddSource.CreateFailed"));
		return;
	}

	titleLabel->setText(QTStr(base.c_str()));
	SetupSource(created.Get());

	if (placeholder) {
		placeholder->setVisible(false);
	}
	body->setVisible(true);
	primaryButton->setEnabled(true);
}

void VadocastSourceDialog::SetupSource(obs_source_t *src)
{
	{
		std::lock_guard<std::mutex> lock(sourceMutex);
		source = src;
	}
	obs_source_inc_showing(src);
	showing = true;

	nameEdit->setText(QT_UTF8(obs_source_get_name(src)));
	if (!addMode) {
		titleLabel->setText(QT_UTF8(obs_source_get_name(src)));
	}

	const uint32_t caps = obs_source_get_output_flags(src);
	const obs_source_type type = obs_source_get_type(src);
	const bool drawable = (caps & OBS_SOURCE_VIDEO) != 0 &&
			      (type == OBS_SOURCE_TYPE_INPUT || type == OBS_SOURCE_TYPE_SCENE);
	previewFrame->setVisible(drawable);

	const std::vector<std::string> *whitelist = WhitelistFor(src);
	PropertiesReloadCallback reload = (PropertiesReloadCallback)obs_source_properties;
	if (whitelist) {
		PropertyFilter filter;
		filter.whitelist = whitelist;
		PropertyFilters()[src] = filter;
		reload = FilteredSourceProperties;
	}

	OBSDataAutoRelease settings = obs_source_get_settings(src);
	OBSPropertiesView *newView = new OBSPropertiesView(settings.Get(), src, reload, (PropertiesUpdateCallback) nullptr,
							   (PropertiesVisualUpdateCb)obs_source_update);
	newView->setMinimumHeight(120);
	settingsLayout->addWidget(newView);
	view = newView;

	/* A modified callback can show properties again: hide them before the queued refresh rebuilds the widgets */
	connect(newView, &OBSPropertiesView::Changed, this, [this]() { Refilter(); }, Qt::QueuedConnection);
	connect(newView, &OBSPropertiesView::Changed, this, [this]() {
		if (view && view->DeferUpdate()) {
			deferTimer->start();
		}
	});

	updatePropertiesSignal.Connect(obs_source_get_signal_handler(src), "update_properties",
				       VadocastSourceDialog::UpdateProperties, this);
}

void VadocastSourceDialog::Refilter()
{
	OBSSource current;
	{
		std::lock_guard<std::mutex> lock(sourceMutex);
		current = source;
	}
	if (!current) {
		return;
	}
	auto it = PropertyFilters().find(current.Get());
	if (it != PropertyFilters().end() && it->second.last && it->second.whitelist) {
		HideNotListed(it->second.last, *it->second.whitelist);
	}
}

void VadocastSourceDialog::ClearSource(bool removeSource)
{
	updatePropertiesSignal.Disconnect();
	if (deferTimer) {
		deferTimer->stop();
	}

	OBSSource old;
	{
		std::lock_guard<std::mutex> lock(sourceMutex);
		old = source;
		source = nullptr;
	}

	if (old) {
		PropertyFilters().erase(old.Get());
	}
	if (OBSPropertiesView *oldView = view.data()) {
		settingsLayout->removeWidget(oldView);
		oldView->hide();
		oldView->deleteLater();
		view.clear();
	}

	if (old) {
		if (showing) {
			obs_source_dec_showing(old);
		}
		if (removeSource) {
			obs_source_remove(old);
		}
	}
	showing = false;
}

void VadocastSourceDialog::Cleanup()
{
	removedSignal.Disconnect();
	if (drawCallbackAdded && preview && preview->GetDisplay()) {
		obs_display_remove_draw_callback(preview->GetDisplay(), VadocastSourceDialog::DrawPreview, this);
	}
	drawCallbackAdded = false;
}

struct VadocastAddItemData {
	obs_source_t *source;
	bool fillCanvas;
	float canvasWidth;
	float canvasHeight;
	obs_sceneitem_t *item = nullptr;
};

static void VadocastSetupSceneItem(void *param, obs_scene_t *scene)
{
	VadocastAddItemData *data = static_cast<VadocastAddItemData *>(param);
	obs_sceneitem_t *item = obs_scene_add(scene, data->source);
	if (!item) {
		return;
	}

	const float sourceWidth = (float)obs_source_get_width(data->source);
	const float sourceHeight = (float)obs_source_get_height(data->source);
	const bool tooBig = sourceWidth > data->canvasWidth || sourceHeight > data->canvasHeight;

	obs_transform_info info = {};
	info.rot = 0.0f;
	vec2_set(&info.scale, 1.0f, 1.0f);
	if (data->fillCanvas || tooBig) {
		/* Same as "Fit to screen" */
		vec2_set(&info.pos, 0.0f, 0.0f);
		info.alignment = OBS_ALIGN_LEFT | OBS_ALIGN_TOP;
		info.bounds_type = OBS_BOUNDS_SCALE_INNER;
		info.bounds_alignment = OBS_ALIGN_CENTER;
		vec2_set(&info.bounds, data->canvasWidth, data->canvasHeight);
		info.crop_to_bounds = obs_sceneitem_get_bounds_crop(item);
	} else {
		/* Natural size, centered on the canvas */
		vec2_set(&info.pos, data->canvasWidth * 0.5f, data->canvasHeight * 0.5f);
		info.alignment = OBS_ALIGN_CENTER;
		info.bounds_type = OBS_BOUNDS_NONE;
		info.bounds_alignment = OBS_ALIGN_CENTER;
		vec2_set(&info.bounds, 0.0f, 0.0f);
		info.crop_to_bounds = false;
	}
	obs_sceneitem_set_info2(item, &info);
	obs_sceneitem_set_visible(item, true);

	obs_sceneitem_addref(item);
	data->item = item;
}

void VadocastSourceDialog::AddClicked()
{
	OBSSource src;
	{
		std::lock_guard<std::mutex> lock(sourceMutex);
		src = source;
	}
	if (!src || !scene || !currentType) {
		return;
	}

	if (view && view->DeferUpdate()) {
		view->UpdateSettings();
	}

	std::string name = QT_TO_UTF8(nameEdit->text().trimmed());
	if (name.empty()) {
		const std::string base = std::string("Vadocast.AddSource.Type.") + currentType->key + ".Default";
		name = QT_TO_UTF8(QTStr(base.c_str()));
	}
	if (name != obs_source_get_name(src)) {
		obs_source_set_name(src, UniqueSourceName(name, src).c_str());
	}

	obs_video_info ovi = {};
	obs_get_video_info(&ovi);

	VadocastAddItemData data;
	data.source = src;
	data.fillCanvas = currentType->fillCanvas;
	data.canvasWidth = (float)(ovi.base_width ? ovi.base_width : 1920);
	data.canvasHeight = (float)(ovi.base_height ? ovi.base_height : 1080);

	obs_enter_graphics();
	obs_scene_atomic_update(scene, VadocastSetupSceneItem, &data);
	obs_leave_graphics();

	if (!data.item) {
		OBSMessageBox::warning(this, QTStr("Vadocast.AddSource.Title"), QTStr("Vadocast.AddSource.CreateFailed"));
		return;
	}

	if (verticalCheck && verticalAvailable && verticalCheck->isChecked()) {
		calldata_t cd;
		calldata_init(&cd);
		calldata_set_ptr(&cd, "item", data.item);
		proc_handler_call(obs_get_proc_handler(), "vadocast_vertical_mirror_item", &cd);
		calldata_free(&cd);
	}
	obs_sceneitem_release(data.item);

	/* The source now lives in the scene: keep it */
	ClearSource(false);
	Cleanup();

	if (OBSBasic *main = OBSBasic::Get()) {
		main->SaveProject();
	}
	QDialog::accept();
}

void VadocastSourceDialog::SaveClicked()
{
	OBSSource src;
	{
		std::lock_guard<std::mutex> lock(sourceMutex);
		src = source;
	}
	if (!src) {
		QDialog::accept();
		return;
	}

	if (view && view->DeferUpdate()) {
		view->UpdateSettings();
	}

	const std::string name = QT_TO_UTF8(nameEdit->text().trimmed());
	if (!name.empty() && name != obs_source_get_name(src)) {
		obs_source_set_name(src, UniqueSourceName(name, src).c_str());
	}

	ClearSource(false);
	Cleanup();

	if (OBSBasic *main = OBSBasic::Get()) {
		main->SaveProject();
	}
	QDialog::accept();
}

void VadocastSourceDialog::reject()
{
	if (!addMode) {
		/* Restore the settings the source had when the panel was opened */
		OBSSource src;
		{
			std::lock_guard<std::mutex> lock(sourceMutex);
			src = source;
		}
		if (src && oldSettings) {
			OBSDataAutoRelease settings = obs_source_get_settings(src);
			obs_data_clear(settings);
			obs_source_update(src, oldSettings);
		}
	}

	/* Add mode: the source was never added to a scene, remove it */
	ClearSource(addMode);
	Cleanup();
	QDialog::reject();
}

void VadocastSourceDialog::DrawPreview(void *data, uint32_t cx, uint32_t cy)
{
	VadocastSourceDialog *window = static_cast<VadocastSourceDialog *>(data);
	std::lock_guard<std::mutex> lock(window->sourceMutex);
	obs_source_t *src = window->source;
	if (!src) {
		return;
	}

	uint32_t sourceCX = std::max(obs_source_get_width(src), 1u);
	uint32_t sourceCY = std::max(obs_source_get_height(src), 1u);

	int x, y;
	float scale;
	GetScaleAndCenterPos(sourceCX, sourceCY, cx, cy, x, y, scale);

	const int newCX = int(scale * float(sourceCX));
	const int newCY = int(scale * float(sourceCY));

	gs_viewport_push();
	gs_projection_push();
	const bool previous = gs_set_linear_srgb(true);

	gs_ortho(0.0f, float(sourceCX), 0.0f, float(sourceCY), -100.0f, 100.0f);
	gs_set_viewport(x, y, newCX, newCY);
	obs_source_video_render(src);

	gs_set_linear_srgb(previous);
	gs_projection_pop();
	gs_viewport_pop();
}

void VadocastSourceDialog::UpdateProperties(void *data, calldata_t *)
{
	VadocastSourceDialog *dialog = static_cast<VadocastSourceDialog *>(data);
	QMetaObject::invokeMethod(
		dialog,
		[dialog]() {
			if (dialog->view) {
				dialog->view->ReloadProperties();
			}
		},
		Qt::QueuedConnection);
}

void VadocastSourceDialog::SourceRemoved(void *data, calldata_t *)
{
	VadocastSourceDialog *dialog = static_cast<VadocastSourceDialog *>(data);
	QMetaObject::invokeMethod(dialog, [dialog]() { dialog->reject(); }, Qt::QueuedConnection);
}

bool VadocastSourceDialog::nativeEvent(const QByteArray &, void *message, qintptr *)
{
#ifdef _WIN32
	const MSG &msg = *static_cast<MSG *>(message);
	switch (msg.message) {
	case WM_MOVE:
		for (OBSQTDisplay *const display : findChildren<OBSQTDisplay *>()) {
			display->OnMove();
		}
		break;
	case WM_DISPLAYCHANGE:
		for (OBSQTDisplay *const display : findChildren<OBSQTDisplay *>()) {
			display->OnDisplayChange();
		}
	}
#else
	UNUSED_PARAMETER(message);
#endif

	return false;
}

} // namespace

void VadocastShowAddSource(QWidget *parent, obs_scene_t *scene)
{
	OBSScene target = scene;
	if (!target) {
		if (OBSBasic *main = OBSBasic::Get()) {
			target = main->GetCurrentScene();
		}
	}
	if (!target) {
		return;
	}

	VadocastSourceDialog dialog(parent, target.Get());
	dialog.exec();
}

void VadocastShowSourceSettings(QWidget *parent, obs_source_t *source)
{
	if (!source) {
		return;
	}

	VadocastSourceDialog dialog(parent, source);
	dialog.exec();
}
