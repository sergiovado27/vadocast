/******************************************************************************
    vadocast - "Transmitir" panel UI.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#include "golive-dialog.hpp"
#include "config-utils.hpp"
#include "multistream.hpp"
#include "output-dialog.hpp"
#include "vadocast-limits.h"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/config-file.h>

#include <QButtonGroup>
#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QSize>
#include <QStringList>

config_t *get_user_config(void);

static QString module_text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

static QString resolution_label(uint32_t cx, uint32_t cy)
{
	const uint32_t shortSide = cx < cy ? cx : cy;
	return QString("%1p (%2x%3)").arg(shortSide).arg(cx).arg(cy);
}

static const char *goLiveIdleStyle =
	"QPushButton { background-color: #E50914; border: 1px solid #FF5A62; color: #FFFFFF; font-weight: bold; }"
	"QPushButton:hover { background-color: #F0303A; }"
	"QPushButton:disabled { background-color: #3A3A3A; border-color: #3A3A3A; color: #969696; }";
static const char *goLiveLiveStyle =
	"QPushButton { background-color: #0B0B0B; border: 2px solid #E50914; color: #FFFFFF; font-weight: bold; }"
	"QPushButton:hover { background-color: #1A1A1A; }";

GoLiveDialog::GoLiveDialog(MultistreamDock *dock_, QWidget *parent) : QDialog(parent), dock(dock_)
{
	setWindowTitle(module_text("GoLive.Title"));
	setMinimumWidth(560);

	auto mainLayout = new QVBoxLayout(this);
	mainLayout->setSpacing(10);

	/* Mode */
	auto modeGroup = new QGroupBox(module_text("GoLive.Mode"));
	auto modeLayout = new QVBoxLayout(modeGroup);
	modeHorizontal = new QRadioButton(module_text("GoLive.ModeHorizontal"));
	modeVertical = new QRadioButton(module_text("GoLive.ModeVertical"));
	modeBoth = new QRadioButton(module_text("GoLive.ModeBoth"));
	auto modeButtons = new QButtonGroup(this);
	modeButtons->addButton(modeHorizontal, VADOCAST_MODE_HORIZONTAL);
	modeButtons->addButton(modeVertical, VADOCAST_MODE_VERTICAL);
	modeButtons->addButton(modeBoth, VADOCAST_MODE_BOTH);
	modeLayout->addWidget(modeHorizontal);
	modeLayout->addWidget(modeVertical);
	modeLayout->addWidget(modeBoth);
	connect(modeButtons, &QButtonGroup::idClicked, this, [this](int) {
		if (loading) {
			return;
		}
		SaveMode();
		UpdateGroups();
	});
	mainLayout->addWidget(modeGroup);

	/* Horizontal destinations */
	horizontalGroup = new QGroupBox(module_text("GoLive.Horizontal"));
	auto horizontalLayout = new QVBoxLayout(horizontalGroup);
	auto horizontalHeader = new QHBoxLayout;
	horizontalHeader->addWidget(new QLabel(module_text("GoLive.Quality")));
	horizontalQuality = new QComboBox;
	horizontalHeader->addWidget(horizontalQuality);
	horizontalHeader->addStretch(1);
	horizontalLayout->addLayout(horizontalHeader);
	horizontalList = new QVBoxLayout;
	horizontalList->setSpacing(4);
	horizontalLayout->addLayout(horizontalList);
	addHorizontal = new QPushButton(module_text("GoLive.AddHorizontal"));
	horizontalLayout->addWidget(addHorizontal, 0, Qt::AlignLeft);
	connect(addHorizontal, &QPushButton::clicked, this, [this] { AddDestination(false); });
	connect(horizontalQuality, &QComboBox::currentIndexChanged, this, [this](int) {
		if (!loading) {
			ApplyHorizontalQuality();
		}
	});
	mainLayout->addWidget(horizontalGroup);

	/* Vertical destinations */
	verticalGroup = new QGroupBox(module_text("GoLive.Vertical"));
	auto verticalLayout = new QVBoxLayout(verticalGroup);
	auto verticalHeader = new QHBoxLayout;
	verticalHeader->addWidget(new QLabel(module_text("GoLive.Quality")));
	verticalQuality = new QComboBox;
	verticalHeader->addWidget(verticalQuality);
	verticalHeader->addStretch(1);
	generateVertical = new QPushButton(module_text("GoLive.GenerateVertical"));
	generateVertical->setToolTip(module_text("GoLive.GenerateVerticalTooltip"));
	verticalHeader->addWidget(generateVertical);
	verticalLayout->addLayout(verticalHeader);
	verticalUnavailable = new QLabel(module_text("GoLive.VerticalUnavailable"));
	verticalUnavailable->setWordWrap(true);
	verticalLayout->addWidget(verticalUnavailable);
	verticalList = new QVBoxLayout;
	verticalList->setSpacing(4);
	verticalLayout->addLayout(verticalList);
	addVertical = new QPushButton(module_text("GoLive.AddVertical"));
	verticalLayout->addWidget(addVertical, 0, Qt::AlignLeft);
	connect(addVertical, &QPushButton::clicked, this, [this] { AddDestination(true); });
	connect(generateVertical, &QPushButton::clicked, this, [this] { GenerateVertical(); });
	connect(verticalQuality, &QComboBox::currentIndexChanged, this, [this](int) {
		if (!loading) {
			ApplyVerticalQuality();
		}
	});
	mainLayout->addWidget(verticalGroup);

	/* Go live */
	statusLabel = new QLabel;
	statusLabel->setWordWrap(true);
	mainLayout->addWidget(statusLabel);

	goLiveButton = new QPushButton;
	goLiveButton->setMinimumHeight(44);
	goLiveButton->setAutoDefault(false);
	connect(goLiveButton, &QPushButton::clicked, this, &GoLiveDialog::GoLiveClicked);
	mainLayout->addWidget(goLiveButton);

	connect(&stateTimer, &QTimer::timeout, this, &GoLiveDialog::UpdateLiveState);
	stateTimer.start(500);
}

void GoLiveDialog::Refresh()
{
	loading = true;
	LoadMode();
	LoadQualities();
	RebuildList(false);
	RebuildList(true);
	loading = false;

	firstState = true;
	UpdateLiveState();
}

void GoLiveDialog::LoadMode()
{
	switch (dock->GetGoLiveMode()) {
	case VADOCAST_MODE_HORIZONTAL:
		modeHorizontal->setChecked(true);
		break;
	case VADOCAST_MODE_VERTICAL:
		modeVertical->setChecked(true);
		break;
	default:
		modeBoth->setChecked(true);
		break;
	}
}

void GoLiveDialog::SaveMode()
{
	int mode = VADOCAST_MODE_BOTH;
	if (modeHorizontal->isChecked()) {
		mode = VADOCAST_MODE_HORIZONTAL;
	} else if (modeVertical->isChecked()) {
		mode = VADOCAST_MODE_VERTICAL;
	}
	dock->SetGoLiveMode(mode);
}

void GoLiveDialog::UpdateGroups()
{
	const bool useHorizontal = !modeVertical->isChecked();
	const bool useVertical = !modeHorizontal->isChecked();
	const bool verticalAvailable = verticalUnavailable->isHidden();

	modeHorizontal->setEnabled(!live);
	modeVertical->setEnabled(!live);
	modeBoth->setEnabled(!live);

	horizontalGroup->setEnabled(useHorizontal && !live);
	verticalGroup->setEnabled(useVertical && !live);
	addVertical->setEnabled(verticalAvailable);
	generateVertical->setEnabled(verticalAvailable);
	verticalQuality->setEnabled(verticalAvailable && verticalQuality->count() > 0);

	if (live) {
		statusLabel->setText(module_text("GoLive.Live"));
		return;
	}
	const int horizontalCount = useHorizontal ? horizontalEnabled : 0;
	const int verticalCount = useVertical ? verticalEnabled : 0;
	statusLabel->setText(module_text("GoLive.Summary").arg(horizontalCount).arg(verticalCount));
}

void GoLiveDialog::LoadQualities()
{
	/* Horizontal: scaled (output) resolution of the main canvas, never above the base canvas. */
	horizontalQuality->clear();
	config_t *profile = obs_frontend_get_profile_config();
	const uint32_t baseCX = profile ? (uint32_t)config_get_uint(profile, "Video", "BaseCX") : 0;
	const uint32_t baseCY = profile ? (uint32_t)config_get_uint(profile, "Video", "BaseCY") : 0;
	const uint32_t outCX = profile ? (uint32_t)config_get_uint(profile, "Video", "OutputCX") : 0;
	const uint32_t outCY = profile ? (uint32_t)config_get_uint(profile, "Video", "OutputCY") : 0;

	const size_t horizontalCount = VADOCAST_RESOLUTION_COUNT(vadocast_horizontal_resolutions);
	for (size_t i = horizontalCount; i > 0; i--) {
		const vadocast_resolution &res = vadocast_horizontal_resolutions[i - 1];
		if (baseCX && baseCY && (res.cx > baseCX || res.cy > baseCY)) {
			continue;
		}
		horizontalQuality->addItem(resolution_label(res.cx, res.cy), QSize((int)res.cx, (int)res.cy));
		if (res.cx == outCX && res.cy == outCY) {
			horizontalQuality->setCurrentIndex(horizontalQuality->count() - 1);
		}
	}

	/* Vertical: resolution of the vertical canvas. */
	verticalQuality->clear();
	calldata_t cd;
	calldata_init(&cd);
	if (proc_handler_call(obs_get_proc_handler(), "vadocast_vertical_get_resolution", &cd)) {
		const uint32_t cx = (uint32_t)calldata_int(&cd, "width");
		const uint32_t cy = (uint32_t)calldata_int(&cd, "height");
		const size_t verticalCount = VADOCAST_RESOLUTION_COUNT(vadocast_vertical_resolutions);
		for (size_t i = verticalCount; i > 0; i--) {
			const vadocast_resolution &res = vadocast_vertical_resolutions[i - 1];
			verticalQuality->addItem(resolution_label(res.cx, res.cy), QSize((int)res.cx, (int)res.cy));
			if (res.cx == cx && res.cy == cy) {
				verticalQuality->setCurrentIndex(verticalQuality->count() - 1);
			}
		}
	}
	calldata_free(&cd);
}

void GoLiveDialog::ApplyHorizontalQuality()
{
	if (dock->IsLive()) {
		QMessageBox::information(this, module_text("GoLive.Title"), module_text("GoLive.QualityLocked"));
		loading = true;
		LoadQualities();
		loading = false;
		return;
	}
	const QSize size = horizontalQuality->currentData().toSize();
	config_t *profile = obs_frontend_get_profile_config();
	if (!profile || size.isEmpty()) {
		return;
	}
	config_set_uint(profile, "Video", "OutputCX", (uint64_t)size.width());
	config_set_uint(profile, "Video", "OutputCY", (uint64_t)size.height());
	config_save_safe(profile, "tmp", nullptr);
	obs_frontend_reset_video();
	blog(LOG_INFO, "[Multistream] horizontal quality set to %dx%d", size.width(), size.height());
}

void GoLiveDialog::ApplyVerticalQuality()
{
	const QSize size = verticalQuality->currentData().toSize();
	if (size.isEmpty()) {
		return;
	}
	calldata_t cd;
	calldata_init(&cd);
	calldata_set_int(&cd, "width", size.width());
	calldata_set_int(&cd, "height", size.height());
	const bool handled = proc_handler_call(obs_get_proc_handler(), "vadocast_vertical_set_resolution", &cd);
	const bool success = handled && calldata_bool(&cd, "success");
	calldata_free(&cd);

	if (!success) {
		QMessageBox::warning(this, module_text("GoLive.Title"), module_text("GoLive.QualityFailed"));
		loading = true;
		LoadQualities();
		loading = false;
	}
}

void GoLiveDialog::RebuildList(bool vertical)
{
	QVBoxLayout *list = vertical ? verticalList : horizontalList;
	while (QLayoutItem *item = list->takeAt(0)) {
		if (QWidget *widget = item->widget()) {
			widget->deleteLater();
		}
		delete item;
	}

	obs_data_array_t *destinations = vertical ? dock->GetVerticalDestinations() : dock->GetHorizontalDestinations();
	if (vertical) {
		verticalUnavailable->setVisible(destinations == nullptr);
		verticalEnabled = 0;
	} else {
		horizontalEnabled = 0;
	}

	const size_t count = obs_data_array_count(destinations);
	bool firstEnabled = true;
	for (size_t i = 0; i < count; i++) {
		obs_data_t *destination = obs_data_array_item(destinations, i);
		const bool enabled = vadocast_destination_enabled(destination);
		AddDestinationRow(list, destination, vertical, !vertical && enabled && firstEnabled);
		if (enabled) {
			firstEnabled = false;
			if (vertical) {
				verticalEnabled++;
			} else {
				horizontalEnabled++;
			}
		}
		obs_data_release(destination);
	}

	if (count == 0 && (!vertical || destinations)) {
		auto empty = new QLabel(module_text("GoLive.NoDestinations"));
		empty->setWordWrap(true);
		list->addWidget(empty);
	}
	obs_data_array_release(destinations);

	UpdateGroups();
}

void GoLiveDialog::AddDestinationRow(QVBoxLayout *list, obs_data_t *destination, bool vertical, bool first)
{
	const QString name = QString::fromUtf8(obs_data_get_string(destination, "name"));
	const QString server = QString::fromUtf8(obs_data_get_string(destination, "stream_server"));
	const bool hasKey = *obs_data_get_string(destination, "stream_key") != '\0';

	auto row = new QFrame;
	row->setFrameShape(QFrame::StyledPanel);
	auto rowLayout = new QHBoxLayout(row);
	rowLayout->setContentsMargins(6, 4, 6, 4);

	auto enabled = new QCheckBox;
	enabled->setChecked(vadocast_destination_enabled(destination));
	enabled->setToolTip(module_text("GoLive.EnabledTooltip"));
	rowLayout->addWidget(enabled);

	auto icon = new QLabel;
	icon->setPixmap(ConfigUtils::getPlatformIconFromEndpoint(server).pixmap(24, 24));
	rowLayout->addWidget(icon);

	auto nameLabel = new QLabel(QString("<b>%1</b>").arg(name.toHtmlEscaped()));
	if (first) {
		nameLabel->setToolTip(module_text("GoLive.MainDestinationTooltip"));
	}
	rowLayout->addWidget(nameLabel, 1);

	auto keyLabel = new QLabel(hasKey ? module_text("GoLive.KeySet") : module_text("GoLive.KeyMissing"));
	if (!hasKey) {
		keyLabel->setStyleSheet("color: #FF5A62; font-weight: bold;");
	}
	rowLayout->addWidget(keyLabel);

	auto editButton = new QPushButton(module_text("GoLive.Edit"));
	editButton->setAutoDefault(false);
	rowLayout->addWidget(editButton);

	auto removeButton = new QPushButton(module_text("GoLive.Remove"));
	removeButton->setAutoDefault(false);
	rowLayout->addWidget(removeButton);

	connect(enabled, &QCheckBox::toggled, this,
		[this, vertical, name](bool checked) { SetDestinationEnabled(vertical, name, checked); });
	connect(editButton, &QPushButton::clicked, this, [this, vertical, name] { EditDestination(vertical, name); });
	connect(removeButton, &QPushButton::clicked, this,
		[this, vertical, name] { DeleteDestination(vertical, name); });

	list->addWidget(row);
}

static QStringList destination_names(obs_data_array_t *destinations, const QString &except = QString())
{
	QStringList names;
	const size_t count = obs_data_array_count(destinations);
	for (size_t i = 0; i < count; i++) {
		obs_data_t *destination = obs_data_array_item(destinations, i);
		const QString name = QString::fromUtf8(obs_data_get_string(destination, "name"));
		if (name != except) {
			names.append(name);
		}
		obs_data_release(destination);
	}
	return names;
}

static obs_data_t *find_by_name(obs_data_array_t *destinations, const QString &name, size_t *index = nullptr)
{
	const size_t count = obs_data_array_count(destinations);
	for (size_t i = 0; i < count; i++) {
		obs_data_t *destination = obs_data_array_item(destinations, i);
		if (name == QString::fromUtf8(obs_data_get_string(destination, "name"))) {
			if (index) {
				*index = i;
			}
			return destination;
		}
		obs_data_release(destination);
	}
	return nullptr;
}

void GoLiveDialog::AddDestination(bool vertical)
{
	obs_data_array_t *destinations = vertical ? dock->GetVerticalDestinations() : dock->GetHorizontalDestinations();
	if (!destinations) {
		return;
	}

	auto outputDialog = new OutputDialog(this, destination_names(destinations));
	outputDialog->setWindowModality(Qt::WindowModal);
	outputDialog->setModal(true);
	if (outputDialog->exec() == QDialog::Accepted) {
		obs_data_t *destination = obs_data_create();
		obs_data_set_bool(destination, "enabled", true);
		obs_data_set_string(destination, "name", outputDialog->outputName.toUtf8().constData());
		obs_data_set_string(destination, "stream_server", outputDialog->outputServer.toUtf8().constData());
		obs_data_set_string(destination, "stream_key", outputDialog->outputKey.toUtf8().constData());
		obs_data_array_push_back(destinations, destination);
		obs_data_release(destination);

		if (vertical) {
			dock->SetVerticalDestinations(destinations);
		} else {
			dock->HorizontalDestinationsChanged();
		}
	}
	delete outputDialog;
	obs_data_array_release(destinations);

	RebuildList(vertical);
}

void GoLiveDialog::EditDestination(bool vertical, const QString &name)
{
	obs_data_array_t *destinations = vertical ? dock->GetVerticalDestinations() : dock->GetHorizontalDestinations();
	if (!destinations) {
		return;
	}
	obs_data_t *destination = find_by_name(destinations, name);
	if (!destination) {
		obs_data_array_release(destinations);
		return;
	}

	auto outputDialog = new OutputDialog(this, name, QString::fromUtf8(obs_data_get_string(destination, "stream_server")),
					     QString::fromUtf8(obs_data_get_string(destination, "stream_key")),
					     destination_names(destinations, name));
	outputDialog->setWindowModality(Qt::WindowModal);
	outputDialog->setModal(true);
	if (outputDialog->exec() == QDialog::Accepted) {
		obs_data_set_string(destination, "name", outputDialog->outputName.toUtf8().constData());
		obs_data_set_string(destination, "stream_server", outputDialog->outputServer.toUtf8().constData());
		obs_data_set_string(destination, "stream_key", outputDialog->outputKey.toUtf8().constData());

		if (vertical) {
			dock->SetVerticalDestinations(destinations);
		} else {
			dock->HorizontalDestinationsChanged();
		}
	}
	delete outputDialog;
	obs_data_release(destination);
	obs_data_array_release(destinations);

	RebuildList(vertical);
}

void GoLiveDialog::DeleteDestination(bool vertical, const QString &name)
{
	auto button = QMessageBox::question(this, module_text("GoLive.Title"), module_text("GoLive.RemoveConfirm").arg(name),
					    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
	if (button != QMessageBox::Yes) {
		return;
	}

	obs_data_array_t *destinations = vertical ? dock->GetVerticalDestinations() : dock->GetHorizontalDestinations();
	if (!destinations) {
		return;
	}
	size_t index = 0;
	obs_data_t *destination = find_by_name(destinations, name, &index);
	if (destination) {
		obs_data_release(destination);
		obs_data_array_erase(destinations, index);
		if (vertical) {
			dock->SetVerticalDestinations(destinations);
		} else {
			dock->HorizontalDestinationsChanged();
		}
	}
	obs_data_array_release(destinations);

	RebuildList(vertical);
}

void GoLiveDialog::SetDestinationEnabled(bool vertical, const QString &name, bool enabled)
{
	obs_data_array_t *destinations = vertical ? dock->GetVerticalDestinations() : dock->GetHorizontalDestinations();
	if (!destinations) {
		return;
	}
	obs_data_t *destination = find_by_name(destinations, name);
	if (destination) {
		obs_data_set_bool(destination, "enabled", enabled);
		obs_data_release(destination);
		if (vertical) {
			dock->SetVerticalDestinations(destinations);
		} else {
			dock->HorizontalDestinationsChanged();
		}
	}
	obs_data_array_release(destinations);

	/* Rebuild later: the checkbox that sent this signal is part of the list. */
	QMetaObject::invokeMethod(this, [this, vertical] { RebuildList(vertical); }, Qt::QueuedConnection);
}

void GoLiveDialog::GenerateVertical()
{
	proc_handler_t *ph = obs_get_proc_handler();
	calldata_t cd;
	calldata_init(&cd);
	calldata_set_bool(&cd, "overwrite", false);
	const bool handled = proc_handler_call(ph, "vadocast_vertical_generate_from_main", &cd);
	const long long created = handled ? calldata_int(&cd, "created") : 0;
	calldata_free(&cd);

	if (!handled) {
		QMessageBox::warning(this, module_text("GoLive.Title"), module_text("GoLive.VerticalUnavailable"));
		return;
	}
	if (created > 0) {
		QMessageBox::information(this, module_text("GoLive.Title"),
					 module_text("GoLive.GenerateDone").arg(created));
		return;
	}

	auto button = QMessageBox::question(this, module_text("GoLive.Title"), module_text("GoLive.GenerateNothing"),
					    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
	if (button != QMessageBox::Yes) {
		return;
	}
	calldata_init(&cd);
	calldata_set_bool(&cd, "overwrite", true);
	proc_handler_call(ph, "vadocast_vertical_generate_from_main", &cd);
	const long long regenerated = calldata_int(&cd, "created");
	calldata_free(&cd);
	QMessageBox::information(this, module_text("GoLive.Title"), module_text("GoLive.GenerateDone").arg(regenerated));
}

void GoLiveDialog::UpdateLiveState()
{
	if (!isVisible() && !firstState) {
		return;
	}
	const bool nowLive = dock->IsLive();
	if (!firstState && nowLive == live) {
		return;
	}
	firstState = false;
	live = nowLive;

	goLiveButton->setText(module_text(live ? "GoLive.Stop" : "GoLive.Start"));
	goLiveButton->setStyleSheet(QString::fromUtf8(live ? goLiveLiveStyle : goLiveIdleStyle));
	UpdateGroups();
}

void GoLiveDialog::GoLiveClicked()
{
	if (dock->IsLive()) {
		const bool warnBeforeStreamStop =
			config_get_bool(get_user_config(), "BasicWindow", "WarnBeforeStoppingStream");
		if (warnBeforeStreamStop) {
			auto button = QMessageBox::question(
				this, QString::fromUtf8(obs_frontend_get_locale_string("ConfirmStop.Title")),
				QString::fromUtf8(obs_frontend_get_locale_string("ConfirmStop.Text")),
				QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
			if (button == QMessageBox::No) {
				return;
			}
		}
		dock->GoLiveStop();
		UpdateLiveState();
		return;
	}

	if (dock->GoLiveStart(this)) {
		UpdateLiveState();
		hide();
	}
}
