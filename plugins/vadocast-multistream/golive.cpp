/******************************************************************************
    vadocast - going live with the "Transmitir" panel.

    Horizontal: the first enabled horizontal destination becomes the main
    stream (OBS' own streaming output, so the rest of the app keeps working:
    status bar, stats, reconnects). Every other enabled horizontal
    destination is started as an extra output that shares the main encoders
    once the main stream is up.

    Vertical: every enabled vertical destination is started on the vertical
    canvas through the vadocast_vertical_* proc handlers.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#include "golive-dialog.hpp"
#include "multistream.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/config-file.h>

#include <QMainWindow>
#include <QMessageBox>
#include <QPointer>
#include <QThread>

#include <functional>
#include <string.h>

config_t *get_user_config(void);

static QPointer<MultistreamDock> golive_dock;

static QWidget *main_window()
{
	return static_cast<QWidget *>(obs_frontend_get_main_window());
}

static QString module_text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

/* Proc handlers can be called from any thread; the dock lives on the UI thread. */
static void run_on_ui(std::function<void()> fn)
{
	MultistreamDock *dock = golive_dock.data();
	if (!dock) {
		return;
	}
	if (QThread::currentThread() == dock->thread()) {
		fn();
	} else {
		QMetaObject::invokeMethod(dock, fn, Qt::QueuedConnection);
	}
}

static void proc_golive_toggle(void *, calldata_t *cd)
{
	run_on_ui([] {
		if (golive_dock) {
			golive_dock->ToggleGoLive();
		}
	});
	calldata_set_bool(cd, "success", !golive_dock.isNull());
}

static void proc_golive_start(void *, calldata_t *cd)
{
	run_on_ui([] {
		if (golive_dock) {
			golive_dock->GoLiveStart(nullptr);
		}
	});
	calldata_set_bool(cd, "success", !golive_dock.isNull());
}

static void proc_golive_stop(void *, calldata_t *cd)
{
	run_on_ui([] {
		if (golive_dock) {
			golive_dock->GoLiveStop();
		}
	});
	calldata_set_bool(cd, "success", !golive_dock.isNull());
}

static void proc_golive_active(void *, calldata_t *cd)
{
	MultistreamDock *dock = golive_dock.data();
	calldata_set_bool(cd, "active", dock && dock->IsLive());
}

void MultistreamDock::RegisterGoLiveProcs()
{
	golive_dock = this;

	proc_handler_t *ph = obs_get_proc_handler();
	proc_handler_add(ph, "void vadocast_golive_toggle(out bool success)", proc_golive_toggle, nullptr);
	proc_handler_add(ph, "void vadocast_golive_start(out bool success)", proc_golive_start, nullptr);
	proc_handler_add(ph, "void vadocast_golive_stop(out bool success)", proc_golive_stop, nullptr);
	proc_handler_add(ph, "void vadocast_golive_active(out bool active)", proc_golive_active, nullptr);
}

int MultistreamDock::GetGoLiveMode()
{
	if (!current_config || !obs_data_has_user_value(current_config, "vadocast_mode")) {
		return VADOCAST_MODE_BOTH;
	}
	const int mode = (int)obs_data_get_int(current_config, "vadocast_mode");
	if (mode < VADOCAST_MODE_HORIZONTAL || mode > VADOCAST_MODE_BOTH) {
		return VADOCAST_MODE_BOTH;
	}
	return mode;
}

void MultistreamDock::SetGoLiveMode(int mode)
{
	if (!current_config) {
		return;
	}
	obs_data_set_int(current_config, "vadocast_mode", mode);
	SaveSettings();
}

obs_data_array_t *MultistreamDock::GetHorizontalDestinations()
{
	if (!current_config) {
		return obs_data_array_create();
	}
	obs_data_array_t *destinations = obs_data_get_array(current_config, "outputs");
	if (!destinations) {
		destinations = obs_data_array_create();
		obs_data_set_array(current_config, "outputs", destinations);
	}
	return destinations;
}

void MultistreamDock::HorizontalDestinationsChanged()
{
	SaveSettings();
	LoadSettings();
}

obs_data_array_t *MultistreamDock::GetVerticalDestinations()
{
	calldata_t cd;
	calldata_init(&cd);
	obs_data_array_t *destinations = nullptr;
	if (proc_handler_call(obs_get_proc_handler(), "vadocast_vertical_get_stream_settings", &cd)) {
		destinations = (obs_data_array_t *)calldata_ptr(&cd, "outputs");
	}
	calldata_free(&cd);
	return destinations;
}

bool MultistreamDock::VerticalAvailable()
{
	obs_data_array_t *destinations = GetVerticalDestinations();
	const bool available = destinations != nullptr;
	obs_data_array_release(destinations);
	return available;
}

void MultistreamDock::SetVerticalDestinations(obs_data_array_t *destinations)
{
	calldata_t cd;
	calldata_init(&cd);
	calldata_set_ptr(&cd, "outputs", destinations);
	proc_handler_call(obs_get_proc_handler(), "vadocast_vertical_set_stream_settings", &cd);
	calldata_free(&cd);
	LoadVerticalOutputs(false);
}

bool MultistreamDock::IsLive()
{
	if (obs_frontend_streaming_active() || !pendingHorizontal.empty()) {
		return true;
	}
	for (auto it = outputs.begin(); it != outputs.end(); it++) {
		if (obs_output_active(std::get<obs_output_t *>(*it))) {
			return true;
		}
	}

	proc_handler_t *ph = obs_get_proc_handler();
	calldata_t cd;
	calldata_init(&cd);
	const bool handled = proc_handler_call(ph, "vadocast_vertical_stream_active", &cd);
	const bool verticalActive = handled && calldata_bool(&cd, "active");
	calldata_free(&cd);
	if (handled) {
		return verticalActive;
	}

	/* Fallback: ask for every vertical output. */
	bool active = false;
	obs_data_array_t *destinations = GetVerticalDestinations();
	const size_t count = obs_data_array_count(destinations);
	for (size_t i = 0; !active && i < count; i++) {
		obs_data_t *destination = obs_data_array_item(destinations, i);
		calldata_init(&cd);
		calldata_set_string(&cd, "name", obs_data_get_string(destination, "name"));
		if (proc_handler_call(ph, "vadocast_vertical_get_stream_output", &cd)) {
			obs_output_t *output = (obs_output_t *)calldata_ptr(&cd, "output");
			active = obs_output_active(output);
			obs_output_release(output);
		}
		calldata_free(&cd);
		obs_data_release(destination);
	}
	obs_data_array_release(destinations);
	return active;
}

/* Collects the enabled destinations of a list, refusing ones without a key. */
static bool collect_destinations(obs_data_array_t *destinations, std::vector<std::string> &names, std::string &missingKey)
{
	const size_t count = obs_data_array_count(destinations);
	for (size_t i = 0; i < count; i++) {
		obs_data_t *destination = obs_data_array_item(destinations, i);
		if (vadocast_destination_enabled(destination)) {
			const char *name = obs_data_get_string(destination, "name");
			const char *server = obs_data_get_string(destination, "stream_server");
			const char *key = obs_data_get_string(destination, "stream_key");
			if (!*server || !*key) {
				missingKey = name;
				obs_data_release(destination);
				return false;
			}
			names.push_back(name);
		}
		obs_data_release(destination);
	}
	return true;
}

static obs_data_t *find_destination(obs_data_array_t *destinations, const std::string &name)
{
	const size_t count = obs_data_array_count(destinations);
	for (size_t i = 0; i < count; i++) {
		obs_data_t *destination = obs_data_array_item(destinations, i);
		if (name == obs_data_get_string(destination, "name")) {
			return destination;
		}
		obs_data_release(destination);
	}
	return nullptr;
}

/* Points OBS' own streaming service at the first horizontal destination. */
static void set_main_service(obs_data_t *destination)
{
	const char *server = obs_data_get_string(destination, "stream_server");
	const char *key = obs_data_get_string(destination, "stream_key");
	const bool whip = strstr(server, "whip") != nullptr;

	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "server", server);
	if (whip) {
		obs_data_set_string(settings, "bearer_token", key);
	} else {
		obs_data_set_string(settings, "key", key);
	}
	obs_service_t *service =
		obs_service_create(whip ? "whip_custom" : "rtmp_custom", "vadocast_main_service", settings, nullptr);
	obs_data_release(settings);

	obs_frontend_set_streaming_service(service);
	obs_frontend_save_streaming_service();
	obs_service_release(service);
}

bool MultistreamDock::GoLiveStart(QWidget *parent)
{
	if (IsLive()) {
		return true;
	}

	QWidget *msgParent = parent ? parent : main_window();
	const QString title = module_text("GoLive.Title");
	const int mode = GetGoLiveMode();
	const bool wantHorizontal = mode != VADOCAST_MODE_VERTICAL;
	const bool wantVertical = mode != VADOCAST_MODE_HORIZONTAL;

	std::vector<std::string> horizontalNames;
	std::vector<std::string> verticalNames;
	std::string missingKey;

	if (wantHorizontal) {
		obs_data_array_t *destinations = GetHorizontalDestinations();
		const bool ok = collect_destinations(destinations, horizontalNames, missingKey);
		obs_data_array_release(destinations);
		if (!ok) {
			QMessageBox::warning(msgParent, title,
					     module_text("GoLive.MissingKey").arg(QString::fromUtf8(missingKey.c_str())));
			return false;
		}
		if (horizontalNames.empty()) {
			QMessageBox::warning(msgParent, title, module_text("GoLive.NoHorizontal"));
			return false;
		}
	}

	if (wantVertical) {
		obs_data_array_t *destinations = GetVerticalDestinations();
		if (!destinations) {
			QMessageBox::warning(msgParent, title, module_text("GoLive.VerticalUnavailable"));
			return false;
		}
		const bool ok = collect_destinations(destinations, verticalNames, missingKey);
		obs_data_array_release(destinations);
		if (!ok) {
			QMessageBox::warning(msgParent, title,
					     module_text("GoLive.MissingKey").arg(QString::fromUtf8(missingKey.c_str())));
			return false;
		}
		if (verticalNames.empty()) {
			QMessageBox::warning(msgParent, title, module_text("GoLive.NoVertical"));
			return false;
		}
	}

	const bool warnBeforeStreamStart = config_get_bool(get_user_config(), "BasicWindow", "WarnBeforeStartingStream");
	if (warnBeforeStreamStart && msgParent && msgParent->isVisible()) {
		auto button = QMessageBox::question(msgParent,
						    QString::fromUtf8(obs_frontend_get_locale_string("ConfirmStart.Title")),
						    QString::fromUtf8(obs_frontend_get_locale_string("ConfirmStart.Text")),
						    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
		if (button == QMessageBox::No) {
			return false;
		}
	}

	if (wantHorizontal) {
		obs_data_array_t *destinations = GetHorizontalDestinations();
		obs_data_t *first = find_destination(destinations, horizontalNames.front());
		if (first) {
			set_main_service(first);
			obs_data_release(first);
		}
		obs_data_array_release(destinations);

		blog(LOG_INFO, "[Multistream] going live horizontally to %d destination(s)", (int)horizontalNames.size());
		pendingHorizontal.assign(horizontalNames.begin() + 1, horizontalNames.end());
		obs_frontend_streaming_start();
	}

	if (wantVertical) {
		blog(LOG_INFO, "[Multistream] going live vertically to %d destination(s)", (int)verticalNames.size());
		proc_handler_t *ph = obs_get_proc_handler();
		for (const auto &name : verticalNames) {
			calldata_t cd;
			calldata_init(&cd);
			calldata_set_string(&cd, "name", name.c_str());
			proc_handler_call(ph, "vadocast_vertical_start_stream_output", &cd);
			calldata_free(&cd);
		}
	}

	UpdateMainStreamButton();
	return true;
}

void MultistreamDock::StartPendingHorizontal()
{
	if (pendingHorizontal.empty()) {
		return;
	}
	std::vector<std::string> pending;
	pending.swap(pendingHorizontal);

	obs_data_array_t *destinations = GetHorizontalDestinations();
	suppressConfirm = true;
	for (const auto &name : pending) {
		obs_data_t *destination = find_destination(destinations, name);
		if (!destination) {
			continue;
		}
		QPushButton *button = FindOutputButton(name);
		if (!StartOutput(destination, button)) {
			blog(LOG_WARNING, "[Multistream] could not start horizontal destination '%s'", name.c_str());
		} else if (button) {
			button->setChecked(true);
			outputButtonStyle(button);
		}
		obs_data_release(destination);
	}
	suppressConfirm = false;
	obs_data_array_release(destinations);
}

void MultistreamDock::GoLiveStop()
{
	pendingHorizontal.clear();

	if (obs_frontend_streaming_active()) {
		obs_frontend_streaming_stop();
	}

	for (auto it = outputs.begin(); it != outputs.end(); it++) {
		obs_output_t *output = std::get<obs_output_t *>(*it);
		if (obs_output_active(output)) {
			obs_queue_task(
				OBS_TASK_GRAPHICS, [](void *param) { obs_output_stop((obs_output_t *)param); }, output,
				false);
		}
	}

	obs_data_array_t *destinations = GetVerticalDestinations();
	if (destinations) {
		proc_handler_t *ph = obs_get_proc_handler();
		const size_t count = obs_data_array_count(destinations);
		for (size_t i = 0; i < count; i++) {
			obs_data_t *destination = obs_data_array_item(destinations, i);
			calldata_t cd;
			calldata_init(&cd);
			calldata_set_string(&cd, "name", obs_data_get_string(destination, "name"));
			proc_handler_call(ph, "vadocast_vertical_stop_stream_output", &cd);
			calldata_free(&cd);
			obs_data_release(destination);
		}
		obs_data_array_release(destinations);
	}

	blog(LOG_INFO, "[Multistream] stopped all destinations");
	UpdateMainStreamButton();
}

void MultistreamDock::ToggleGoLive()
{
	if (!IsLive()) {
		ShowGoLive();
		return;
	}

	QWidget *parent = main_window();
	const bool warnBeforeStreamStop = config_get_bool(get_user_config(), "BasicWindow", "WarnBeforeStoppingStream");
	if (warnBeforeStreamStop && parent && parent->isVisible()) {
		auto button = QMessageBox::question(parent,
						    QString::fromUtf8(obs_frontend_get_locale_string("ConfirmStop.Title")),
						    QString::fromUtf8(obs_frontend_get_locale_string("ConfirmStop.Text")),
						    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
		if (button == QMessageBox::No) {
			return;
		}
	}
	GoLiveStop();
}

void MultistreamDock::ShowGoLive()
{
	if (!goLiveDialog) {
		goLiveDialog = new GoLiveDialog(this, main_window());
	}
	goLiveDialog->Refresh();
	goLiveDialog->show();
	goLiveDialog->raise();
	goLiveDialog->activateWindow();
}

QPushButton *MultistreamDock::FindOutputButton(const std::string &name)
{
	const QString qname = QString::fromUtf8(name.c_str());
	for (int i = 1; i < mainCanvasOutputLayout->count(); i++) {
		QLayoutItem *item = mainCanvasOutputLayout->itemAt(i);
		QWidget *streamGroup = item ? item->widget() : nullptr;
		if (streamGroup && streamGroup->objectName() == qname) {
			if (auto button = streamGroup->findChild<QPushButton *>()) {
				return button;
			}
		}
	}

	/* No row in the dock (yet): keep an invisible button to track the state. */
	QPushButton *&hidden = hiddenOutputButtons[name];
	if (!hidden) {
		hidden = new QPushButton(this);
		hidden->setCheckable(true);
		hidden->hide();
	}
	return hidden;
}

/* The main window's stream button only knows about the main (horizontal)
 * stream; keep it in sync when only vertical or extra outputs are live. */
void MultistreamDock::UpdateMainStreamButton()
{
	if (obs_frontend_streaming_active()) {
		return; // OBS manages the button itself while the main stream runs
	}
	QWidget *main = main_window();
	QPushButton *button = main ? main->findChild<QPushButton *>(QStringLiteral("streamButton")) : nullptr;
	if (!button || !button->isEnabled()) {
		return;
	}
	const bool live = IsLive();
	const QString text = QString::fromUtf8(
		obs_frontend_get_locale_string(live ? "Basic.Main.StopStreaming" : "Basic.Main.StartStreaming"));
	if (button->isCheckable() && button->isChecked() != live) {
		button->setChecked(live);
	}
	if (button->text() != text) {
		button->setText(text);
	}
}
