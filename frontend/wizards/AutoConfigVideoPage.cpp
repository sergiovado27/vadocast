#include "AutoConfigVideoPage.hpp"
#include "AutoConfig.hpp"
#include "ui_AutoConfigVideoPage.h"

#include <OBSApp.hpp>

#include <QScreen>

#include <vadocast-limits.h>

#include "moc_AutoConfigVideoPage.cpp"

#define RES_TEXT(x) "Basic.AutoConfig.VideoPage." x
#define RES_USE_CURRENT RES_TEXT("BaseResolution.UseCurrent")
#define RES_USE_DISPLAY RES_TEXT("BaseResolution.Display")
#define FPS_USE_CURRENT RES_TEXT("FPS.UseCurrent")
#define FPS_PREFER_HIGH_FPS RES_TEXT("FPS.PreferHighFPS")
#define FPS_PREFER_HIGH_RES RES_TEXT("FPS.PreferHighRes")

#define wiz reinterpret_cast<AutoConfig *>(wizard())

AutoConfigVideoPage::AutoConfigVideoPage(QWidget *parent) : QWizardPage(parent), ui(new Ui_AutoConfigVideoPage)
{
	ui->setupUi(this);

	setTitle(QTStr("Basic.AutoConfig.VideoPage"));
	setSubTitle(QTStr("Basic.AutoConfig.VideoPage.SubTitle"));

	obs_video_info ovi;
	obs_get_video_info(&ovi);

	long double fpsVal = (long double)ovi.fps_num / (long double)ovi.fps_den;

	QString fpsStr = (ovi.fps_den > 1) ? QString::number(fpsVal, 'f', 2) : QString::number(fpsVal, 'g');

	ui->fps->addItem(QTStr(FPS_PREFER_HIGH_FPS), (int)AutoConfig::FPSType::PreferHighFPS);
	ui->fps->addItem(QTStr(FPS_PREFER_HIGH_RES), (int)AutoConfig::FPSType::PreferHighRes);
	ui->fps->addItem(QTStr(FPS_USE_CURRENT).arg(fpsStr), (int)AutoConfig::FPSType::UseCurrent);
	ui->fps->addItem(QStringLiteral("30"), (int)AutoConfig::FPSType::fps30);
	ui->fps->addItem(QStringLiteral("60"), (int)AutoConfig::FPSType::fps60);
	ui->fps->setCurrentIndex(0);

	QString cxStr = QString::number(ovi.base_width);
	QString cyStr = QString::number(ovi.base_height);

	int encRes = int(ovi.base_width << 16) | int(ovi.base_height);

	// vadocast: only the supported horizontal canvases are offered (current one first if it is supported).
	if (vadocast_horizontal_allowed(ovi.base_width, ovi.base_height)) {
		ui->canvasRes->addItem(QTStr(RES_USE_CURRENT).arg(cxStr, cyStr), (int)encRes);
	}

	auto addRes = [&](int cx, int cy) {
		encRes = (cx << 16) | cy;
		QString str = QString("%1x%2").arg(QString::number(cx), QString::number(cy));
		ui->canvasRes->addItem(str, encRes);
	};

	const size_t resCount = VADOCAST_RESOLUTION_COUNT(vadocast_horizontal_resolutions);
	for (size_t i = resCount; i > 0; i--) {
		addRes(vadocast_horizontal_resolutions[i - 1].cx, vadocast_horizontal_resolutions[i - 1].cy);
	}

	ui->canvasRes->setCurrentIndex(0);
}

AutoConfigVideoPage::~AutoConfigVideoPage() {}

int AutoConfigVideoPage::nextId() const
{
	return wiz->type == AutoConfig::Type::Recording ? AutoConfig::TestPage : AutoConfig::StreamPage;
}

bool AutoConfigVideoPage::validatePage()
{
	int encRes = ui->canvasRes->currentData().toInt();
	wiz->baseResolutionCX = encRes >> 16;
	wiz->baseResolutionCY = encRes & 0xFFFF;
	wiz->fpsType = (AutoConfig::FPSType)ui->fps->currentData().toInt();

	obs_video_info ovi;
	obs_get_video_info(&ovi);

	switch (wiz->fpsType) {
	case AutoConfig::FPSType::PreferHighFPS:
		wiz->specificFPSNum = 0;
		wiz->specificFPSDen = 0;
		wiz->preferHighFPS = true;
		break;
	case AutoConfig::FPSType::PreferHighRes:
		wiz->specificFPSNum = 0;
		wiz->specificFPSDen = 0;
		wiz->preferHighFPS = false;
		break;
	case AutoConfig::FPSType::UseCurrent:
		wiz->specificFPSNum = ovi.fps_num;
		wiz->specificFPSDen = ovi.fps_den;
		wiz->preferHighFPS = false;
		break;
	case AutoConfig::FPSType::fps30:
		wiz->specificFPSNum = 30;
		wiz->specificFPSDen = 1;
		wiz->preferHighFPS = false;
		break;
	case AutoConfig::FPSType::fps60:
		wiz->specificFPSNum = 60;
		wiz->specificFPSDen = 1;
		wiz->preferHighFPS = false;
		break;
	}

	return true;
}
