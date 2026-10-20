#include "output-dialog.hpp"

#include <QAction>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <obs-module.h>

namespace {

QString vcText(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

// Same effect as setClasses() from qt-wrappers.hpp (this module does not link
// qt-wrappers); widgets are not polished yet, so setting the property is enough.
void setClass(QWidget *widget, const char *classes)
{
	widget->setProperty("class", QString::fromUtf8(classes));
}

QLabel *makeErrorLabel()
{
	auto label = new QLabel;
	setClass(label, "vc-caption");
	label->setStyleSheet(QStringLiteral("color: #F0303A;"));
	label->setWordWrap(true);
	label->setVisible(false);
	return label;
}

void setError(QLabel *label, const QString &message)
{
	label->setText(message);
	label->setVisible(!message.isEmpty());
}

bool hasValidScheme(const QString &server)
{
	static const char *schemes[] = {"rtmp://", "rtmps://", "srt://", "http://", "https://"};
	for (const char *scheme : schemes) {
		const QString prefix = QString::fromLatin1(scheme);
		if (server.startsWith(prefix, Qt::CaseInsensitive) && server.size() > prefix.size()) {
			return true;
		}
	}
	return false;
}

} // namespace

OutputDialog::OutputDialog(QDialog *parent, QStringList _otherNames) : QDialog(parent), otherNames(_otherNames)
{
	buildUi(false);
}

// Edit mode
OutputDialog::OutputDialog(QDialog *parent, QString name, QString server, QString key, QStringList _otherNames)
	: QDialog(parent),
	  otherNames(_otherNames),
	  outputName(name),
	  outputServer(server),
	  outputKey(key)
{
	buildUi(true);
}

OutputDialog::~OutputDialog() {}

void OutputDialog::buildUi(bool edit)
{
	const QString title = vcText(edit ? "OutputDialog.EditTitle" : "OutputDialog.AddTitle");
	setWindowTitle(title);
	setModal(true);
	setMinimumWidth(480);

	auto root = new QVBoxLayout(this);
	root->setContentsMargins(16, 16, 16, 16);
	root->setSpacing(12);

	auto titleLabel = new QLabel(title);
	setClass(titleLabel, "vc-title");
	root->addWidget(titleLabel);

	// Form card
	auto card = new QFrame;
	setClass(card, "vc-card");
	auto form = new QVBoxLayout(card);
	form->setContentsMargins(12, 12, 12, 12);
	form->setSpacing(4);

	auto addField = [form](const char *labelKey, QLineEdit *field, QLabel *error, bool last) {
		auto label = new QLabel(vcText(labelKey));
		setClass(label, "vc-label");
		label->setBuddy(field);
		setClass(field, "vc-input");
		form->addWidget(label);
		form->addWidget(field);
		form->addWidget(error);
		if (!last) {
			form->addSpacing(8);
		}
	};

	nameEdit = new QLineEdit(outputName);
	nameEdit->setPlaceholderText(vcText("OutputDialog.NamePlaceholder"));
	nameError = makeErrorLabel();
	addField("OutputDialog.Name", nameEdit, nameError, false);

	serverEdit = new QLineEdit(outputServer);
	serverEdit->setPlaceholderText(vcText("OutputDialog.ServerPlaceholder"));
	serverError = makeErrorLabel();
	addField("OutputDialog.Server", serverEdit, serverError, false);

	keyEdit = new QLineEdit(outputKey);
	keyEdit->setPlaceholderText(vcText("OutputDialog.KeyPlaceholder"));
	keyVisibilityAction = keyEdit->addAction(QIcon(QStringLiteral(":/vadocast-multistream/media/eye.svg")),
						 QLineEdit::TrailingPosition);
	keyVisibilityAction->setCheckable(true);
	keyError = makeErrorLabel();
	addField("OutputDialog.Key", keyEdit, keyError, true);
	setKeyVisible(false);

	form->addSpacing(8);
	auto help = new QLabel(vcText("OutputDialog.Help"));
	setClass(help, "vc-caption");
	help->setWordWrap(true);
	form->addWidget(help);

	root->addWidget(card);
	root->addStretch(1);

	// Footer: actions on the right, primary action last
	auto footer = new QHBoxLayout;
	footer->setSpacing(8);
	footer->addStretch(1);

	auto cancelButton = new QPushButton(vcText("OutputDialog.Cancel"));
	setClass(cancelButton, "vc-secondary");
	cancelButton->setAutoDefault(false);
	footer->addWidget(cancelButton);

	saveButton = new QPushButton(vcText("OutputDialog.Save"));
	setClass(saveButton, "vc-primary");
	saveButton->setDefault(true);
	footer->addWidget(saveButton);

	root->addLayout(footer);

	connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
	connect(saveButton, &QPushButton::clicked, this, &OutputDialog::save);
	connect(keyVisibilityAction, &QAction::toggled, this, &OutputDialog::setKeyVisible);

	connect(nameEdit, &QLineEdit::textChanged, this, [this]() {
		nameTouched = true;
		validate();
	});
	connect(serverEdit, &QLineEdit::textChanged, this, [this]() {
		serverTouched = true;
		validate();
	});
	connect(keyEdit, &QLineEdit::textChanged, this, [this]() {
		keyTouched = true;
		validate();
	});

	// When editing, show problems with the stored values right away.
	nameTouched = serverTouched = keyTouched = edit;
	validate();

	nameEdit->setFocus();
}

void OutputDialog::setKeyVisible(bool visible)
{
	keyEdit->setEchoMode(visible ? QLineEdit::Normal : QLineEdit::Password);
	keyVisibilityAction->setIcon(QIcon(QString::fromLatin1(visible ? ":/vadocast-multistream/media/eye-off.svg"
								       : ":/vadocast-multistream/media/eye.svg")));
	const QString tip = vcText(visible ? "OutputDialog.HideKey" : "OutputDialog.ShowKey");
	keyVisibilityAction->setToolTip(tip);
	keyVisibilityAction->setText(tip);
}

bool OutputDialog::validate()
{
	const QString name = nameEdit->text().trimmed();
	const QString server = serverEdit->text().trimmed();
	const QString key = keyEdit->text().trimmed();

	QString nameMessage;
	if (name.isEmpty()) {
		nameMessage = vcText("OutputDialog.NameRequired");
	} else if (otherNames.contains(name)) {
		nameMessage = vcText("OutputDialog.NameTaken");
	}

	QString serverMessage;
	if (server.isEmpty()) {
		serverMessage = vcText("OutputDialog.ServerRequired");
	} else if (!hasValidScheme(server)) {
		serverMessage = vcText("OutputDialog.ServerInvalid");
	}

	QString keyMessage;
	if (key.isEmpty()) {
		keyMessage = vcText("OutputDialog.KeyRequired");
	}

	setError(nameError, nameTouched ? nameMessage : QString());
	setError(serverError, serverTouched ? serverMessage : QString());
	setError(keyError, keyTouched ? keyMessage : QString());

	const bool valid = nameMessage.isEmpty() && serverMessage.isEmpty() && keyMessage.isEmpty();
	saveButton->setEnabled(valid);
	return valid;
}

void OutputDialog::save()
{
	nameTouched = serverTouched = keyTouched = true;
	if (!validate()) {
		return;
	}

	outputName = nameEdit->text().trimmed();
	outputServer = serverEdit->text().trimmed();
	outputKey = keyEdit->text().trimmed();
	accept();
}
