#include "scene-collection-manager.hpp"

#include <qabstractbutton.h>
#include <QDir>
#include <QMenu>
#include <QMessageBox>
#include <QInputDialog>

#include "obs-frontend-api.h"
#include "obs-module.h"
#include "obs.hpp"
#include "version.h"
#include "util/config-file.h"
#include "util/platform.h"

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("Exeldro");
OBS_MODULE_USE_DEFAULT_LOCALE("scene-collection-manager", "en-US")

bool obs_module_load()
{
	blog(LOG_INFO, "[Scene Collection Manager] loaded version %s",
	     PROJECT_VERSION);
	QAction *action =
		static_cast<QAction *>(obs_frontend_add_tools_menu_qaction(
			obs_module_text("SceneCollectionManager")));

	auto cb = [] {
		obs_frontend_push_ui_translation(obs_module_get_string);

		auto *scmd = new SceneCollectionManagerDialog(
			static_cast<QMainWindow *>(
				obs_frontend_get_main_window()));
		scmd->show();
		scmd->setAttribute(Qt::WA_DeleteOnClose, true);
		obs_frontend_pop_ui_translation();
	};

	QAction::connect(action, &QAction::triggered, cb);
	return true;
}

void obs_module_unload() {}

MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("Description");
}

MODULE_EXPORT const char *obs_module_name(void)
{
	return obs_module_text("SceneCollectionManager");
}

bool GetFileSafeName(const char *name, std::string &file)
{
	size_t base_len = strlen(name);
	size_t len = os_utf8_to_wcs(name, base_len, nullptr, 0);
	std::wstring wfile;

	if (!len)
		return false;

	wfile.resize(len);
	os_utf8_to_wcs(name, base_len, &wfile[0], len + 1);

	for (size_t i = wfile.size(); i > 0; i--) {
		size_t im1 = i - 1;

		if (iswspace(wfile[im1])) {
			wfile[im1] = '_';
		} else if (wfile[im1] != '_' && !iswalnum(wfile[im1])) {
			wfile.erase(im1, 1);
		}
	}

	if (wfile.size() == 0)
		wfile = L"characters_only";

	len = os_wcs_to_utf8(wfile.c_str(), wfile.size(), nullptr, 0);
	if (!len)
		return false;

	file.resize(len);
	os_wcs_to_utf8(wfile.c_str(), wfile.size(), &file[0], len + 1);
	return true;
}

void SceneCollectionManagerDialog::RefreshSceneCollections()
{
	const auto current_scene_collection =
		QString::fromUtf8(obs_frontend_get_current_scene_collection());
	const auto filter = ui->searchSceneCollectionEdit->text();
	ui->sceneCollectionList->clear();
	for (auto &scene_collection : scene_collections) {
		if (!filter.isEmpty() && !scene_collection.first.contains(
						 filter, Qt::CaseInsensitive))
			continue;
		auto *item = new QListWidgetItem(scene_collection.first,
						 ui->sceneCollectionList);
		ui->sceneCollectionList->addItem(item);
		if (scene_collection.first == current_scene_collection) {
			ui->sceneCollectionList->setItemSelected(item, true);
			ui->sceneCollectionList->setCurrentItem(item);
		}
	}
}

std::string
SceneCollectionManagerDialog::GetBackupDirectory(std::string filename)
{

	auto l = filename.length();
	if (filename.compare(l - 5, 5, ".json") == 0) {
		filename.resize(l - 5);
		filename.append("/");
	}
	return filename;
}

void SceneCollectionManagerDialog::on_searchSceneCollectionEdit_textChanged(
	const QString &text)
{
	RefreshSceneCollections();
}

void SceneCollectionManagerDialog::on_actionAddSceneCollection_triggered()
{
	QMenu m;
	m.addAction(QString::fromUtf8(obs_module_text("Add")));
	//obs_frontend_add_scene_collection(nullptr);
	m.addAction(QString::fromUtf8(obs_module_text("Import")));
	m.addAction(QString::fromUtf8(obs_module_text("Duplicate")));
	m.exec(QCursor::pos());
}

void SceneCollectionManagerDialog::on_actionRemoveSceneCollection_triggered()
{
	if (const auto item = ui->sceneCollectionList->currentItem()) {
		QMessageBox remove(this);
		remove.setText(QString::fromUtf8(
			obs_module_text("DoYouWantToRemoveSceneCollection")));
		QPushButton *yes = remove.addButton(
			QString::fromUtf8(obs_module_text("Yes")),
			QMessageBox::YesRole);
		remove.setDefaultButton(yes);
		remove.addButton(QString::fromUtf8(obs_module_text("No")),
				 QMessageBox::NoRole);
		remove.setIcon(QMessageBox::Question);
		remove.setWindowTitle(
			QString::fromUtf8(obs_module_text("ConfirmRemove")));
		remove.exec();

		if (reinterpret_cast<QAbstractButton *>(yes) !=
		    remove.clickedButton())
			return;

		const auto filePath = scene_collections.at(item->text());
		if (filePath.length() == 0)
			return;

		os_unlink(filePath.c_str());
		scene_collections.erase(item->text());
		RefreshSceneCollections();
	}
}

void SceneCollectionManagerDialog::on_actionConfigSceneCollection_triggered()
{
	QMenu m;
	m.addAction(QString::fromUtf8(obs_module_text("Rename")));
	m.addAction(QString::fromUtf8(obs_module_text("Export")));
	m.exec(QCursor::pos());
}

void SceneCollectionManagerDialog::on_actionSwitchSceneCollection_triggered()
{
	if (const auto item = ui->sceneCollectionList->currentItem()) {
		obs_frontend_set_current_scene_collection(
			item->text().toUtf8().constData());
	}
}

void SceneCollectionManagerDialog::on_actionAddBackup_triggered()
{
	if (const auto item = ui->sceneCollectionList->currentItem()) {
		const auto filename = scene_collections.at(item->text());
		if (!filename.length())
			return;
		bool ok;
		std::string defaultName = os_generate_formatted_filename(
			"", true, "%CCYY-%MM-%DD %hh:%mm:%ss");
		defaultName.resize(defaultName.length() - 1);
		QString text = QInputDialog::getText(
			this, QString::fromUtf8(obs_module_text("Backup")),
			QString::fromUtf8(obs_module_text("BackupName")),
			QLineEdit::Normal,
			QString::fromUtf8(defaultName.c_str()), &ok);
		if (!ok || text.isEmpty())
			return;

		const auto backupDir = GetBackupDirectory(filename);
		os_mkdirs(backupDir.c_str());

		std::string safeName;
		if (!GetFileSafeName(text.toUtf8().constData(), safeName))
			return;

		auto *data = obs_data_create_from_json_file_safe(
			filename.c_str(), "bak");
		obs_data_set_string(data, "name", text.toUtf8().constData());
		const auto backupFile = backupDir + safeName + ".json";
		obs_data_save_json(data, backupFile.c_str());
		obs_data_release(data);
		ui->backupList->addItem(text);
	}
}

void SceneCollectionManagerDialog::on_actionRemoveBackup_triggered()
{
	if (const auto item = ui->sceneCollectionList->currentItem()) {
		const auto filename = scene_collections.at(item->text());
		if (!filename.length())
			return;

		if (auto backupItem = ui->backupList->currentItem()) {
			QMessageBox remove(this);
			remove.setText(QString::fromUtf8(
				obs_module_text("DoYouWantToRemoveBackup")));
			QPushButton *yes = remove.addButton(
				QString::fromUtf8(obs_module_text("Yes")),
				QMessageBox::YesRole);
			remove.setDefaultButton(yes);
			remove.addButton(
				QString::fromUtf8(obs_module_text("No")),
				QMessageBox::NoRole);
			remove.setIcon(QMessageBox::Question);
			remove.setWindowTitle(QString::fromUtf8(
				obs_module_text("ConfirmRemove")));
			remove.exec();

			if (reinterpret_cast<QAbstractButton *>(yes) !=
			    remove.clickedButton())
				return;

			const auto backupDir = GetBackupDirectory(filename);
			std::string safeName;
			if (!GetFileSafeName(
				    backupItem->text().toUtf8().constData(),
				    safeName))
				return;

			const auto backupFile = backupDir + safeName + ".json";
			os_unlink(backupFile.c_str());
			on_sceneCollectionList_currentRowChanged(
				ui->sceneCollectionList->currentRow());
		}
	}
}

void SceneCollectionManagerDialog::on_actionSwitchBackup_triggered()
{

	if (const auto item = ui->sceneCollectionList->currentItem()) {
		const auto filename = scene_collections.at(item->text());
		if (!filename.length())
			return;

		if (auto backupItem = ui->backupList->currentItem()) {
			const auto backupDir = GetBackupDirectory(filename);
			std::string safeName;
			if (!GetFileSafeName(
				    backupItem->text().toUtf8().constData(),
				    safeName))
				return;

			const auto backupFile = backupDir + safeName + ".json";
			auto *data = obs_data_create_from_json_file(
				backupFile.c_str());
			obs_data_set_string(data, "name",
					    item->text().toUtf8().constData());
			obs_data_save_json_safe(data, filename.c_str(), "tmp",
						"bak");
			obs_data_release(data);
			config_set_string(obs_frontend_get_global_config(),
					  "Basic", "SceneCollection", "");
			config_set_string(obs_frontend_get_global_config(),
					  "Basic", "SceneCollectionFile",
					  "scene_collection_manager_temp");
			obs_frontend_set_current_scene_collection(
				item->text().toUtf8().constData());
			char path[512];
			int ret = os_get_config_path(
				path, sizeof(path),
				"obs-studio/basic/scenes/scene_collection_manager_temp.json");
			if (ret <= 0) {
				return;
			}
			os_unlink(path);
		}
	}
}

void SceneCollectionManagerDialog::on_sceneCollectionList_currentRowChanged(
	int currentRow)
{
	ui->backupList->clear();
	if (currentRow <= -1)
		return;
	if (const auto item = ui->sceneCollectionList->currentItem()) {
		const auto filename = scene_collections.at(item->text());
		if (!filename.length())
			return;
		auto backupDir = GetBackupDirectory(filename);
		auto f = backupDir + "*.json";
		os_glob_t *glob;
		if (os_glob(f.c_str(), 0, &glob) != 0) {
			return;
		}
		for (size_t i = 0; i < glob->gl_pathc; i++) {
			const char *filePath = glob->gl_pathv[i].path;

			if (glob->gl_pathv[i].directory)
				continue;
			auto *data = obs_data_create_from_json_file_safe(
				filePath, "bak");
			std::string name = obs_data_get_string(data, "name");
			obs_data_release(data);

			/* if no name found, use the file name as the name
			 * (this only happens when switching to the new version) */
			if (name.empty()) {
				name = strrchr(filePath, '/') + 1;
				name.resize(name.size() - 5);
			}
			ui->backupList->addItem(
				QString::fromUtf8(name.c_str()));
		}
	}
}

SceneCollectionManagerDialog::SceneCollectionManagerDialog(QMainWindow *parent)
	: QDialog(parent), ui(new Ui::SceneCollectionManagerDialog)
{
	ui->setupUi(this);

	char path[512];
	int ret = os_get_config_path(path, sizeof(path),
				     "obs-studio/basic/scenes/*.json");
	if (ret <= 0) {
		blog(LOG_WARNING, "Failed to get config path for scene "
				  "collections");
		return;
	}
	os_glob_t *glob;
	if (os_glob(path, 0, &glob) != 0) {
		blog(LOG_WARNING, "Failed to glob scene collections");
		return;
	}

	for (size_t i = 0; i < glob->gl_pathc; i++) {
		const char *filePath = glob->gl_pathv[i].path;

		if (glob->gl_pathv[i].directory)
			continue;

		auto *data =
			obs_data_create_from_json_file_safe(filePath, "bak");
		std::string name = obs_data_get_string(data, "name");
		obs_data_release(data);

		/* if no name found, use the file name as the name
		 * (this only happens when switching to the new version) */
		if (name.empty()) {
			name = strrchr(filePath, '/') + 1;
			name.resize(name.size() - 5);
		}
		this->scene_collections[QString::fromUtf8(name.c_str())] =
			filePath;
	}
	RefreshSceneCollections();
}

SceneCollectionManagerDialog::~SceneCollectionManagerDialog() {}
