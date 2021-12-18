#pragma once

#include "ui_SceneCollectionManager.h"

#include <QDialog>
#include <QWidget>
#include <QMainWindow>
#include <memory>

class SceneCollectionManagerDialog : public QDialog {
	Q_OBJECT
private:
	std::unique_ptr<Ui::SceneCollectionManagerDialog> ui;
	std::map<QString, std::string> scene_collections;
	void RefreshSceneCollections();
	std::string GetBackupDirectory(std::string filename);
private slots:
	void on_searchSceneCollectionEdit_textChanged(const QString &text);

	void on_actionAddSceneCollection_triggered();
	void on_actionRemoveSceneCollection_triggered();
	void on_actionConfigSceneCollection_triggered();
	void on_actionSwitchSceneCollection_triggered();

	void on_actionAddBackup_triggered();
	void on_actionRemoveBackup_triggered();
	void on_actionSwitchBackup_triggered();

	void on_sceneCollectionList_currentRowChanged(int currentRow);

public:
	SceneCollectionManagerDialog(QMainWindow *parent = nullptr);
	~SceneCollectionManagerDialog();
};
