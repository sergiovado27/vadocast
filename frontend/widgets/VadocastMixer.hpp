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

#pragma once

#include <obs.hpp>
#include <obs-frontend-api.h>

#include <QFrame>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QWidget>

#include <atomic>
#include <unordered_map>
#include <vector>

class QLabel;
class QPushButton;
class QSlider;
class QToolButton;
class QVBoxLayout;
class QScrollArea;

/* One card in the mixer: a single audio source. */
class VadocastMixerItem : public QFrame {
	Q_OBJECT

public:
	enum class Kind { Microphone, Desktop, Media, Other };

	VadocastMixerItem(obs_source_t *source, Kind kind, QWidget *parent = nullptr);
	~VadocastMixerItem();

	const QString &uuid() const { return sourceUuid; }
	Kind kind() const { return sourceKind; }

	void setKind(Kind kind);
	void setDisplayName(const QString &name);
	void setHiddenInMixer(bool hidden);
	void setVolumeLocked(bool locked);

private:
	OBSWeakSource weakSource;
	QString sourceUuid;
	QString fullName;
	Kind sourceKind;
	bool hiddenInMixer = false;
	bool muted = false;

	obs_fader_t *fader = nullptr;
	obs_volmeter_t *volmeter = nullptr;
	std::atomic<float> peakDb{-100.0f};
	float shownLevel = 0.0f;

	std::vector<OBSSignal> sourceSignals;

	QToolButton *expandButton = nullptr;
	QLabel *nameLabel = nullptr;
	QToolButton *muteButton = nullptr;
	QToolButton *menuButton = nullptr;
	QSlider *slider = nullptr;
	QLabel *valueLabel = nullptr;
	QWidget *meter = nullptr;
	QTimer meterTimer;

	void updateElidedName();
	void updateMuteButton();
	void updateValueLabel();
	void setExpanded(bool expanded);
	void showMenu();

	static void obsFaderChanged(void *data, float db);
	static void obsVolmeterUpdated(void *data, const float magnitude[MAX_AUDIO_CHANNELS],
				       const float peak[MAX_AUDIO_CHANNELS], const float inputPeak[MAX_AUDIO_CHANNELS]);
	static void obsSourceMuted(void *data, calldata_t *params);

protected:
	void resizeEvent(QResizeEvent *event) override;

private slots:
	void syncVolumeFromSource();
	void syncMuteFromSource();
	void sliderMoved(int value);
	void muteClicked();
	void updateMeter();
};

/* The "Sonido" panel. Finds and tracks audio sources by itself. */
class VadocastMixer : public QWidget {
	Q_OBJECT

public:
	explicit VadocastMixer(QWidget *parent = nullptr);
	~VadocastMixer();

signals:
	void addAudioRequested();

private:
	std::vector<OBSSignal> globalSignals;
	std::vector<OBSSignal> previewSignals;
	bool frontendCallbackRegistered = false;
	bool shuttingDown = false;
	bool showHidden = false;

	std::unordered_map<QString, QPointer<VadocastMixerItem>> items;

	QScrollArea *scrollArea = nullptr;
	QLabel *sectionTitles[4] = {};
	QWidget *sectionContainers[4] = {};
	QVBoxLayout *sectionLayouts[4] = {};
	QLabel *emptyLabel = nullptr;
	QPushButton *addButton = nullptr;
	QPushButton *hiddenButton = nullptr;

	QTimer refreshTimer;

	void connectSignals();
	void disconnectAll();
	void updatePreviewHandlers();
	void clearItems();

	static void onFrontendEvent(enum obs_frontend_event event, void *data);
	static void obsGlobalSourceSignal(void *data, calldata_t *params);
	static void obsPreviewItemVisible(void *data, calldata_t *params);

private slots:
	void queueRefresh();
	void refresh();
	void handleFrontendEvent(int event);
};
