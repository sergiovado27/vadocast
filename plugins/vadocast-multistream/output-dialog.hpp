#pragma once

#include <QDialog>
#include <QWidget>
#include <QStackedWidget>
#include <QToolButton>
#include <QComboBox>
#include <QPushButton>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QString>
#include "obs-data.h"

class OutputDialog : public QDialog {
	Q_OBJECT
private:
	QWidget *WizardServicePage();

	QToolButton *selectionButton(std::string title, QIcon icon, int selectionStep);

	QWidget *WizardInfoKick(bool edit = false);
	QWidget *WizardInfoYouTube(bool edit = false);
	QWidget *WizardInfoTwitter(bool edit = false);
	QWidget *WizardInfoUnknown(bool edit = false);
	QWidget *WizardInfoTwitch(bool edit = false);
	QWidget *WizardInfoTrovo(bool edit = false);
	QWidget *WizardInfoTikTok(bool edit = false);
	QWidget *WizardInfoFacebook(bool edit = false);

	// Platform icons
	QIcon platformIconTwitch = QIcon(":/vadocast-multistream/media/twitch.png");
	QIcon platformIconYouTube = QIcon(":/vadocast-multistream/media/youtube.png");
	QIcon platformIconKick = QIcon(":/vadocast-multistream/media/kick.png");
	QIcon platformIconTikTok = QIcon(":/vadocast-multistream/media/tiktok.png");
	QIcon platformIconTwitter = QIcon(":/vadocast-multistream/media/twitter.png");
	QIcon platformIconTrovo = QIcon(":/vadocast-multistream/media/trovo.png");
	QIcon platformIconFacebook = QIcon(":/vadocast-multistream/media/facebook.png");
	QIcon platformIconUnknown = QIcon(":/vadocast-multistream/media/unknown.png");

	obs_data_array_t *servicesData;

	void resetOutputs();
	void acceptOutputs();
	void validateOutputs(QPushButton *confirmButton);

	// Generators
	QLineEdit *generateOutputNameField(std::string text, QPushButton *confirmButton, bool edit = false);
	QLineEdit *generateOutputServerField(QPushButton *confirmButton, bool locked, bool edit = false);
	QComboBox *generateOutputServerCombo(std::string service, QPushButton *confirmButton, bool edit = false);
	QLineEdit *generateOutputKeyField(QPushButton *confirmButton, bool edit = false);
	QHBoxLayout *generateWizardButtonLayout(QPushButton *confirmButton, QPushButton *serviceButton, bool edit);
	QPushButton *generateBackButton();

	obs_data_t *getService(std::string serviceName);

	QStackedWidget *stackedWidget;
	QStringList otherNames;

public:
	OutputDialog(QDialog *parent, QStringList otherNames);
	OutputDialog(QDialog *parent, QString name, QString server, QString key, QStringList otherNames);
	~OutputDialog();

	QString outputName;
	QString outputServer;
	QString outputKey;
};
