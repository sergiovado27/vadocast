/******************************************************************************
    vadocast - "Transmitir" panel: one place to choose horizontal, vertical or
    both, manage the destinations (each with its own stream key) of every
    orientation and go live with a single button.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#pragma once

#include <obs.h>

#include <QComboBox>
#include <QDialog>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QString>
#include <QTimer>
#include <QVBoxLayout>

class MultistreamDock;

/* Values stored in the profile config as "vadocast_mode". */
enum vadocast_golive_mode {
	VADOCAST_MODE_HORIZONTAL = 0,
	VADOCAST_MODE_VERTICAL = 1,
	VADOCAST_MODE_BOTH = 2,
};

/* A destination without an explicit "enabled" value is enabled. */
static inline bool vadocast_destination_enabled(obs_data_t *destination)
{
	return !obs_data_has_user_value(destination, "enabled") || obs_data_get_bool(destination, "enabled");
}

class GoLiveDialog : public QDialog {
	Q_OBJECT

public:
	explicit GoLiveDialog(MultistreamDock *dock, QWidget *parent = nullptr);

	/* Reloads modes, qualities and destination lists from the current profile. */
	void Refresh();

private:
	MultistreamDock *dock;

	QRadioButton *modeHorizontal = nullptr;
	QRadioButton *modeVertical = nullptr;
	QRadioButton *modeBoth = nullptr;

	QGroupBox *horizontalGroup = nullptr;
	QGroupBox *verticalGroup = nullptr;
	QComboBox *horizontalQuality = nullptr;
	QComboBox *verticalQuality = nullptr;
	QVBoxLayout *horizontalList = nullptr;
	QVBoxLayout *verticalList = nullptr;
	QPushButton *addHorizontal = nullptr;
	QPushButton *addVertical = nullptr;
	QPushButton *generateVertical = nullptr;
	QLabel *verticalUnavailable = nullptr;

	QPushButton *goLiveButton = nullptr;
	QLabel *statusLabel = nullptr;

	QTimer stateTimer;
	bool live = false;
	bool loading = false;
	bool firstState = true;
	int horizontalEnabled = 0;
	int verticalEnabled = 0;

	void LoadMode();
	void SaveMode();
	void UpdateGroups();

	void LoadQualities();
	void ApplyHorizontalQuality();
	void ApplyVerticalQuality();

	void RebuildList(bool vertical);
	void AddDestinationRow(QVBoxLayout *list, obs_data_t *destination, bool vertical, bool first);
	void AddDestination(bool vertical);
	void EditDestination(bool vertical, const QString &name);
	void DeleteDestination(bool vertical, const QString &name);
	void SetDestinationEnabled(bool vertical, const QString &name, bool enabled);

	void GenerateVertical();

	void UpdateLiveState();
	void GoLiveClicked();
};
