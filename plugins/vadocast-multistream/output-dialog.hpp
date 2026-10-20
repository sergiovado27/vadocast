#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>

class QAction;
class QLabel;
class QLineEdit;
class QPushButton;

// "Añadir destino" / "Editar destino": one simple form with name, server URL
// and stream key. Callers construct it and call exec(); on QDialog::Accepted
// the values are in outputName, outputServer and outputKey.
class OutputDialog : public QDialog {
	Q_OBJECT

private:
	QStringList otherNames;

	QLineEdit *nameEdit = nullptr;
	QLineEdit *serverEdit = nullptr;
	QLineEdit *keyEdit = nullptr;
	QAction *keyVisibilityAction = nullptr;

	QLabel *nameError = nullptr;
	QLabel *serverError = nullptr;
	QLabel *keyError = nullptr;

	QPushButton *saveButton = nullptr;

	bool nameTouched = false;
	bool serverTouched = false;
	bool keyTouched = false;

	void buildUi(bool edit);
	void setKeyVisible(bool visible);
	bool validate();
	void save();

public:
	OutputDialog(QDialog *parent, QStringList otherNames);
	OutputDialog(QDialog *parent, QString name, QString server, QString key, QStringList otherNames);
	~OutputDialog();

	QString outputName;
	QString outputServer;
	QString outputKey;
};
