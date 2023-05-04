#pragma once

#include "ui_SceneCollectionManager.h"

#include <QDialog>
#include <QWidget>
#include <QMainWindow>
#include <memory>
#include "obs.h"

class SceneCollectionManagerDialog : public QDialog {
	Q_OBJECT
private:
	std::unique_ptr<Ui::SceneCollectionManagerDialog> ui;
	std::map<QString, std::string> scene_collections;
	void ReadSceneCollections();
	void RefreshSceneCollections();
	void import_parts(obs_data_t *data, const char *dir);
	void try_fix_paths(obs_data_t *data, const char *dir,
			   char *path_buffer);
	bool replace_source(obs_data_t *s, const char *id, const char *find,
			    const char *replace, bool cs = true);
	void replace_os_specific(obs_data_t *data);
	void make_source_mac(obs_data_t *data);
	void make_source_windows(obs_data_t *data);
	void make_source_linux(obs_data_t *data);
	void export_local_files(obs_data_t *data, std::string dir,
				std::string subdir);
private slots:
	void on_searchSceneCollectionEdit_textChanged(const QString &text);

	void on_actionAddSceneCollection_triggered();
	void on_actionAddNewSceneCollection_triggered();
	void on_actionImportSceneCollection_triggered();
	void on_actionDuplicateSceneCollection_triggered();
	void on_actionRemoveSceneCollection_triggered();
	void on_actionConfigSceneCollection_triggered();
	void on_actionRenameSceneCollection_triggered();
	void on_actionExportSceneCollection_triggered();
	void on_actionSwitchSceneCollection_triggered();

	void on_actionAddBackup_triggered();
	void on_actionRemoveBackup_triggered();
	void on_actionConfigBackup_triggered();
	void on_actionRenameBackup_triggered();
	void on_actionSwitchBackup_triggered();

	void on_sceneCollectionList_currentRowChanged(int currentRow);
	void on_sceneCollectionList_itemDoubleClicked(QListWidgetItem *item);

	void on_backupList_itemDoubleClicked(QListWidgetItem *item);

public:
	SceneCollectionManagerDialog(QMainWindow *parent = nullptr);
	~SceneCollectionManagerDialog();
};
