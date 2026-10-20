#pragma once

#include "config-dialog.hpp"
#include "golive-dialog.hpp"
#include "obs-websocket-api.h"
#include <obs.h>
#include <obs-frontend-api.h>
#include <QFrame>
#include <QPointer>
#include <QPushButton>
#include <QString>
#include <QTimer>
#include <QVBoxLayout>

#include <map>
#include <string>
#include <vector>

class OBSBasicSettings;

class MultistreamDock : public QFrame {
	Q_OBJECT

private:
	OBSBasicSettings *configDialog = nullptr;

	obs_data_t *current_config = nullptr;

	QVBoxLayout *mainLayout = nullptr;
	QVBoxLayout *mainCanvasLayout = nullptr;
	QVBoxLayout *mainCanvasOutputLayout = nullptr;
	QVBoxLayout *verticalCanvasLayout = nullptr;
	QVBoxLayout *verticalCanvasOutputLayout = nullptr;
	QPushButton *mainStreamButton = nullptr;
	QPushButton *configButton = nullptr;
	QLabel *mainPlatformIconLabel = nullptr;
	QString mainPlatformUrl;

	QTimer videoCheckTimer;
	video_t *mainVideo = nullptr;
	std::vector<video_t *> oldVideo;

	std::vector<std::tuple<std::string, obs_output_t *, QPushButton *>> outputs;
	obs_data_array_t *vertical_outputs = nullptr;
	bool exiting = false;
	bool finished_loading = false;

	obs_websocket_vendor vendor = nullptr;

	/* vadocast "Transmitir" panel state */
	QPointer<GoLiveDialog> goLiveDialog;
	std::vector<std::string> pendingHorizontal; // started once the main stream is up
	std::map<std::string, QPushButton *> hiddenOutputButtons;
	bool suppressConfirm = false;

	QPushButton *FindOutputButton(const std::string &name);
	void StartPendingHorizontal();
	void UpdateMainStreamButton();
	bool StartVerticalDestinations(QWidget *parent);

	void LoadSettingsFile();
	void LoadSettings();
	void LoadOutput(obs_data_t *data, bool vertical);
	void SaveSettings();

	bool StartOutput(obs_data_t *settings, QPushButton *streamButton);

	void outputButtonStyle(QPushButton *button);

	void storeMainStreamEncoders();

	QIcon streamActiveIcon = QIcon(":/vadocast-multistream/media/streaming.svg");
	QIcon streamInactiveIcon = QIcon(":/vadocast-multistream/media/stream.svg");

	static void frontend_event(enum obs_frontend_event event, void *private_data);

	static void stream_output_stop(void *data, calldata_t *calldata);
	static void stream_output_start(void *data, calldata_t *calldata);

	static void vendor_request_version(obs_data_t *request_data, obs_data_t *response_data, void *self);
	static void vendor_request_get_outputs(obs_data_t *request_data, obs_data_t *response_data, void *self);
	static void vendor_request_start_output(obs_data_t *request_data, obs_data_t *response_data, void *self);
	static void vendor_request_stop_output(obs_data_t *request_data, obs_data_t *response_data, void *self);

public:
	MultistreamDock(QWidget *parent = nullptr);
	~MultistreamDock();
	void LoadVerticalOutputs(bool firstLoad = true);
	void LoadWebsocket();

	/* vadocast "Transmitir" panel (implemented in golive.cpp) */
	int GetGoLiveMode();
	void SetGoLiveMode(int mode);
	obs_data_array_t *GetHorizontalDestinations(); // caller releases
	void HorizontalDestinationsChanged();
	bool VerticalAvailable();
	obs_data_array_t *GetVerticalDestinations(); // caller releases, nullptr without vertical canvas
	void SetVerticalDestinations(obs_data_array_t *destinations);
	bool IsLive();
	bool GoLiveStart(QWidget *parent);
	void GoLiveStop();
	void ToggleGoLive();
	void ShowGoLive();
	void RegisterGoLiveProcs();
};

class AspectRatioPixmapLabel : public QLabel {
	Q_OBJECT
public:
	explicit AspectRatioPixmapLabel(QWidget *parent = 0);
	virtual int heightForWidth(int width) const;
	virtual QSize sizeHint() const;
	QPixmap scaledPixmap() const;
public slots:
	void setPixmap(const QPixmap &);
	void resizeEvent(QResizeEvent *);

private:
	QPixmap pix;
};
