/******************************************************************************
    Copyright (C) 2023 by Lain Bailey <lain@obsproject.com>
                          Zachary Lund <admin@computerquip.com>
                          Philippe Groarke <philippe.groarke@gmail.com>

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

#include "OBSBasic.hpp"

#include <qt-wrappers.hpp>

void setupDockAction(QDockWidget *dock)
{
	QAction *action = dock->toggleViewAction();

	auto neverDisable = [action]() {
		QSignalBlocker block(action);
		action->setEnabled(true);
	};

	auto newToggleView = [dock](bool check) {
		QSignalBlocker block(dock);
		dock->setVisible(check);
	};

	// Replace the slot connected by default
	QObject::disconnect(action, &QAction::triggered, nullptr, 0);
	QObject::connect(action, &QAction::triggered, dock, newToggleView);

	// Make the action unable to be disabled
	QObject::connect(action, &QAction::enabledChanged, action, neverDisable);
}

void OBSBasic::on_resetDocks_triggered(bool force)
{
#ifdef BROWSER_AVAILABLE
	if ((extraDocks.size() || extraCustomDocks.size() || extraBrowserDocks.size()) && !force)
#else
	if ((extraDocks.size() || extraCustomDocks.size()) && !force)
#endif
	{
		QMessageBox::StandardButton button =
			OBSMessageBox::question(this, QTStr("ResetUIWarning.Title"), QTStr("ResetUIWarning.Text"));

		if (button == QMessageBox::No) {
			return;
		}
	}

#define RESET_DOCKLIST(dockList)                                                                               \
	for (int i = dockList.size() - 1; i >= 0; i--) {                                                       \
		dockList[i]->setVisible(true);                                                                 \
		dockList[i]->setFloating(true);                                                                \
		dockList[i]->move(frameGeometry().topLeft() + rect().center() - dockList[i]->rect().center()); \
		dockList[i]->setVisible(false);                                                                \
	}

	RESET_DOCKLIST(extraDocks)
	RESET_DOCKLIST(extraCustomDocks)
#ifdef BROWSER_AVAILABLE
	RESET_DOCKLIST(extraBrowserDocks)
#endif
#undef RESET_DOCKLIST

	restoreState(startingDockLayout);
	ui->sideDocks->setChecked(true);

	int cx = width();
	int bottomDocksHeight = height();

	bottomDocksHeight = bottomDocksHeight * 225 / 1000;

	ui->scenesDock->setVisible(true);
	ui->sourcesDock->setVisible(true);
	ui->mixerDock->setVisible(true);
	ui->transitionsDock->setVisible(true);
	controlsDock->setVisible(true);
	statsDock->setVisible(false);
	statsDock->setFloating(true);

	QList<QDockWidget *> bottomDocks{ui->mixerDock, ui->transitionsDock, controlsDock};

	resizeDocks(bottomDocks, {bottomDocksHeight, bottomDocksHeight, bottomDocksHeight}, Qt::Vertical);
	resizeDocks(bottomDocks, {cx * 45 / 100, cx * 14 / 100, cx * 16 / 100}, Qt::Horizontal);

	int sideDockWidth = std::min(width() * 30 / 100, 280);
	resizeDocks({ui->scenesDock, ui->sourcesDock}, {sideDockWidth, sideDockWidth}, Qt::Horizontal);

	activateWindow();
}

void OBSBasic::on_lockDocks_toggled(bool lock)
{
	QDockWidget::DockWidgetFeatures features =
		lock ? QDockWidget::NoDockWidgetFeatures
		     : (QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable |
			QDockWidget::DockWidgetFloatable);

	QDockWidget::DockWidgetFeatures mainFeatures = features;
	mainFeatures &= ~QDockWidget::QDockWidget::DockWidgetClosable;

	ui->scenesDock->setFeatures(mainFeatures);
	ui->sourcesDock->setFeatures(mainFeatures);
	ui->mixerDock->setFeatures(mainFeatures);
	ui->transitionsDock->setFeatures(mainFeatures);
	controlsDock->setFeatures(mainFeatures);
	statsDock->setFeatures(features);

	for (int i = extraDocks.size() - 1; i >= 0; i--) {
		extraDocks[i]->setFeatures(features);
	}

	for (int i = extraCustomDocks.size() - 1; i >= 0; i--) {
		extraCustomDocks[i]->setFeatures(features);
	}

#ifdef BROWSER_AVAILABLE
	for (int i = extraBrowserDocks.size() - 1; i >= 0; i--) {
		extraBrowserDocks[i]->setFeatures(features);
	}
#endif
}

void OBSBasic::on_sideDocks_toggled(bool side)
{
	config_set_bool(App()->GetUserConfig(), "BasicWindow", "SideDocks", side);

	setDockCornersVertical(side);
}

void OBSBasic::AddDockWidget(QDockWidget *dock, Qt::DockWidgetArea area, bool extraBrowser)
{
	if (dock->objectName().isEmpty()) {
		return;
	}

	bool lock = ui->lockDocks->isChecked();
	QDockWidget::DockWidgetFeatures features =
		lock ? QDockWidget::NoDockWidgetFeatures
		     : (QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable |
			QDockWidget::DockWidgetFloatable);

	setupDockAction(dock);
	dock->setFeatures(features);
	addDockWidget(area, dock);

#ifdef BROWSER_AVAILABLE
	if (extraBrowser && extraBrowserMenuDocksSeparator.isNull()) {
		extraBrowserMenuDocksSeparator = ui->menuDocks->addSeparator();
	}

	if (!extraBrowser && !extraBrowserMenuDocksSeparator.isNull()) {
		ui->menuDocks->insertAction(extraBrowserMenuDocksSeparator, dock->toggleViewAction());
	} else {
		ui->menuDocks->addAction(dock->toggleViewAction());
	}

	if (extraBrowser) {
		return;
	}
#else
	UNUSED_PARAMETER(extraBrowser);

	ui->menuDocks->addAction(dock->toggleViewAction());
#endif

	extraDockNames.push_back(dock->objectName());
	extraDocks.push_back(std::shared_ptr<QDockWidget>(dock));
}

void OBSBasic::RemoveDockWidget(const QString &name)
{
	if (extraDockNames.contains(name)) {
		int idx = extraDockNames.indexOf(name);
		extraDockNames.removeAt(idx);
		extraDocks[idx].reset();
		extraDocks.removeAt(idx);
	} else if (extraCustomDockNames.contains(name)) {
		int idx = extraCustomDockNames.indexOf(name);
		extraCustomDockNames.removeAt(idx);
		removeDockWidget(extraCustomDocks[idx]);
		extraCustomDocks.removeAt(idx);
	}
}

bool OBSBasic::IsDockObjectNameUsed(const QString &name)
{
	QStringList list;
	list << "scenesDock"
	     << "sourcesDock"
	     << "mixerDock"
	     << "transitionsDock"
	     << "controlsDock"
	     << "statsDock";
	list << extraDockNames;
	list << extraCustomDockNames;

	return list.contains(name);
}

void OBSBasic::AddCustomDockWidget(QDockWidget *dock)
{
	// Prevent the object name from being changed
	connect(dock, &QObject::objectNameChanged, this, &OBSBasic::RepairCustomExtraDockName);

	bool lock = ui->lockDocks->isChecked();
	QDockWidget::DockWidgetFeatures features =
		lock ? QDockWidget::NoDockWidgetFeatures
		     : (QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable |
			QDockWidget::DockWidgetFloatable);

	dock->setFeatures(features);
	addDockWidget(Qt::RightDockWidgetArea, dock);

	extraCustomDockNames.push_back(dock->objectName());
	extraCustomDocks.push_back(dock);
}

void OBSBasic::setDockCornersVertical(bool vertical)
{
	if (vertical) {
		setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
		setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
		setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
		setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
	} else {
		setCorner(Qt::TopLeftCorner, Qt::TopDockWidgetArea);
		setCorner(Qt::TopRightCorner, Qt::TopDockWidgetArea);
		setCorner(Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
		setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);
	}
}

void OBSBasic::RepairCustomExtraDockName()
{
	QDockWidget *dock = reinterpret_cast<QDockWidget *>(sender());
	int idx = extraCustomDocks.indexOf(dock);
	QSignalBlocker block(dock);

	if (idx == -1) {
		blog(LOG_WARNING, "A custom dock got its object name changed");
		return;
	}

	blog(LOG_WARNING, "The custom dock '%s' got its object name restored", QT_TO_UTF8(extraCustomDockNames[idx]));

	dock->setObjectName(extraCustomDockNames[idx]);
}

/* ------------------------------------------------------------------------- */
/* vadocast: fixed "live studio" layout                                      */
/*                                                                           */
/*  left:   Escenas / Fuentes (+ Fuentes verticales as a tab)                */
/*  center: horizontal preview                                               */
/*  right:  vertical preview | Sonido above the Transmitir controls          */
/*                                                                           */
/* Docks cannot be moved, floated or closed, and OBS-only panels (scene      */
/* transitions, stats, multistream dock, vertical scenes/transitions) are    */
/* hidden. The layout is re-applied on every start, so a saved OBS dock      */
/* state never brings the old look back.                                     */
/* ------------------------------------------------------------------------- */

static QDockWidget *FindExtraDock(QMainWindow *window, const char *name)
{
	return window->findChild<QDockWidget *>(QString::fromUtf8(name), Qt::FindDirectChildrenOnly);
}

static void LockDock(QDockWidget *dock)
{
	if (!dock) {
		return;
	}
	dock->setFloating(false);
	dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
	dock->toggleViewAction()->setVisible(false);
}

void OBSBasic::VadocastApplyLayout()
{
	/* Menus: only "File" (settings, exit) and "Help" remain. */
	ui->menuBasic_MainMenu_Edit->menuAction()->setVisible(false);
	ui->viewMenu->menuAction()->setVisible(false);
	ui->menuDocks->menuAction()->setVisible(false);
	ui->profileMenu->menuAction()->setVisible(false);
	ui->sceneCollectionMenu->menuAction()->setVisible(false);
	ui->menuTools->menuAction()->setVisible(false);
	ui->actionShow_Recordings->setVisible(false);
	ui->actionRemux->setVisible(false);
	ui->actionShowProfileFolder->setVisible(false);

	/* Controls: only "Transmitir" and settings (no recording, replay buffer,
	 * virtual camera, studio mode or YouTube broadcast management). */
	if (controlsDock) {
		static const char *hiddenControls[] = {"broadcastButton",  "recordButton",           "pauseRecordButton",
						       "replayBufferButton", "saveReplayButton",       "virtualCamButton",
						       "virtualCamConfigButton", "modeSwitch",          "exitButton"};
		for (const char *name : hiddenControls) {
			if (QWidget *button = controlsDock->findChild<QWidget *>(QString::fromUtf8(name))) {
				button->setVisible(false);
			}
		}
		if (QWidget *streamButton = controlsDock->findChild<QWidget *>(QStringLiteral("streamButton"))) {
			setClasses(streamButton, "vc-primary");
			streamButton->setMinimumHeight(44);
		}
	}

	/* Preview: no OBS context toolbar or zoom/scaling bar under it. */
	ui->contextContainer->setVisible(false);
	config_set_bool(App()->GetUserConfig(), "BasicWindow", "ShowContextToolbars", false);
	static const char *hiddenPreviewWidgets[] = {"previewScalePercent", "previewScalingMode", "previewZoomInButton",
						     "previewZoomOutButton"};
	for (const char *name : hiddenPreviewWidgets) {
		if (QWidget *widget = findChild<QWidget *>(QString::fromUtf8(name))) {
			widget->setVisible(false);
		}
	}

	/* Panels that are not part of vadocast. */
	ui->transitionsDock->setVisible(false);
	ui->transitionsDock->toggleViewAction()->setVisible(false);
	if (statsDock) {
		statsDock->setVisible(false);
	}
	static const char *hiddenExtraDocks[] = {"VadocastMultistreamDock", "VerticalCanvasDockScenes",
						 "VerticalCanvasDockTransitions"};
	for (const char *name : hiddenExtraDocks) {
		if (QDockWidget *dock = FindExtraDock(this, name)) {
			dock->setFloating(false);
			dock->setVisible(false);
			dock->toggleViewAction()->setVisible(false);
		}
	}

	QDockWidget *verticalDock = FindExtraDock(this, "VerticalCanvasDock");
	QDockWidget *verticalSources = FindExtraDock(this, "VerticalCanvasDockSources");

	setDockCornersVertical(true);

	/* Left column: scenes above sources. */
	addDockWidget(Qt::LeftDockWidgetArea, ui->scenesDock);
	splitDockWidget(ui->scenesDock, ui->sourcesDock, Qt::Vertical);
	ui->scenesDock->setVisible(true);
	ui->sourcesDock->setVisible(true);
	if (verticalSources) {
		verticalSources->setFloating(false);
		tabifyDockWidget(ui->sourcesDock, verticalSources);
		verticalSources->setVisible(true);
		ui->sourcesDock->raise();
	}

	/* Right side: vertical preview, then sound above the go-live controls. */
	QDockWidget *rightColumnAnchor = ui->mixerDock;
	if (verticalDock) {
		verticalDock->setFloating(false);
		addDockWidget(Qt::RightDockWidgetArea, verticalDock);
		verticalDock->setVisible(true);
		splitDockWidget(verticalDock, ui->mixerDock, Qt::Horizontal);
	} else {
		addDockWidget(Qt::RightDockWidgetArea, ui->mixerDock);
	}
	ui->mixerDock->setVisible(true);
	if (controlsDock) {
		splitDockWidget(rightColumnAnchor, controlsDock, Qt::Vertical);
		controlsDock->setVisible(true);
	}

	/* Sizes: side columns compact, previews get the rest. */
	const int sideWidth = std::max(240, width() * 18 / 100);
	resizeDocks({ui->scenesDock, ui->sourcesDock}, {sideWidth, sideWidth}, Qt::Horizontal);
	resizeDocks({ui->mixerDock}, {std::max(280, width() * 20 / 100)}, Qt::Horizontal);
	if (verticalDock) {
		resizeDocks({verticalDock}, {std::max(260, width() * 18 / 100)}, Qt::Horizontal);
	}
	resizeDocks({ui->scenesDock, ui->sourcesDock}, {height() * 35 / 100, height() * 65 / 100}, Qt::Vertical);
	if (controlsDock) {
		resizeDocks({ui->mixerDock, controlsDock}, {height() * 75 / 100, height() * 25 / 100}, Qt::Vertical);
	}

	/* Lock everything in place. */
	ui->lockDocks->setChecked(true);
	ui->lockDocks->setVisible(false);
	ui->sideDocks->setVisible(false);
	ui->resetDocks->setVisible(false);
	LockDock(ui->scenesDock);
	LockDock(ui->sourcesDock);
	LockDock(ui->mixerDock);
	LockDock(controlsDock);
	LockDock(verticalDock);
	LockDock(verticalSources);
}

void OBSBasic::VadocastForceCutTransition()
{
	/* vadocast has no scene transitions: scenes always switch instantly. */
	if (cutTransition) {
		SetTransition(OBSSource(cutTransition));
	}
}
