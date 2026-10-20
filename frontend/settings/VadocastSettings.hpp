/******************************************************************************
    vadocast - simple settings dialog (Vídeo, Audio, Teclas rápidas, General).
    Replaces the big OBS settings window.

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

#pragma once

#include <obs.hpp>

#include <QDialog>
#include <QPointer>

#include <string>
#include <vector>

class OBSBasic;
class OBSHotkeyWidget;
class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QStackedWidget;
class QTabBar;
class QVBoxLayout;
class QWidget;

class VadocastSettings : public QDialog {
	Q_OBJECT

public:
	enum Section { Video = 0, Audio = 1, Hotkeys = 2, General = 3 };

	explicit VadocastSettings(QWidget *parent = nullptr);
	~VadocastSettings() override;

	void setSection(Section section);

	/* Opens the dialog modally on the given section. */
	static void openAt(QWidget *parent, Section section);

private:
	struct HotkeyEntry {
		QPointer<OBSHotkeyWidget> widget;
		obs_hotkey_id id = OBS_INVALID_HOTKEY_ID;
		/* Second hotkey that receives the same bindings (mute / unmute pair act as a toggle). */
		obs_hotkey_id mirrorId = OBS_INVALID_HOTKEY_ID;
		/* Frontend hotkeys are stored in the profile ("Hotkeys" section). */
		bool frontend = false;
		std::string name;
	};

	OBSBasic *main = nullptr;

	QListWidget *nav = nullptr;
	QStackedWidget *pages = nullptr;

	/* Vídeo */
	QLabel *videoLockedCaption = nullptr;
	QTabBar *videoTabs = nullptr;
	QStackedWidget *videoTabPages = nullptr;
	QComboBox *hResolution = nullptr;
	QComboBox *hFps = nullptr;
	QComboBox *hBitrate = nullptr;
	QLabel *hBitrateCaption = nullptr;
	QComboBox *hEncoder = nullptr;
	QComboBox *vResolution = nullptr;
	QComboBox *vBitrate = nullptr;
	QLabel *vBitrateCaption = nullptr;
	QComboBox *vEncoder = nullptr;
	QLabel *vUnavailableCaption = nullptr;
	bool videoLocked = false;
	bool verticalResAvailable = false;
	bool verticalEncAvailable = false;
	uint32_t loadedVCX = 0;
	uint32_t loadedVCY = 0;
	int loadedVBitrate = 0;
	std::string loadedVEncoder;

	/* Audio */
	QComboBox *micDevice = nullptr;
	QComboBox *desktopDevice = nullptr;
	QComboBox *audioBitrate = nullptr;

	/* Teclas rápidas */
	std::vector<HotkeyEntry> hotkeyEntries;

	/* General */
	QComboBox *language = nullptr;
	QCheckBox *warnBeforeStop = nullptr;
	QCheckBox *warnBeforeStart = nullptr;
	QCheckBox *minimizeToTray = nullptr;

	QWidget *BuildVideoPage();
	QWidget *BuildAudioPage();
	QWidget *BuildHotkeysPage();
	QWidget *BuildGeneralPage();

	void LoadVideo();
	void LoadAudio();
	void LoadGeneral();

	void UpdateBitrateCaptions();

	void SaveVideo(bool &resetVideo, bool &resetOutputs);
	void SaveVertical();
	void SaveAudio(bool &resetOutputs);
	void SaveHotkeys();
	void SaveGeneral();

	void Save();
};
