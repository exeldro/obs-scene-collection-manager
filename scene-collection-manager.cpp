#include "scene-collection-manager.hpp"

#include <qabstractbutton.h>
#include <QDir>
#include <QMenu>
#include <QMessageBox>
#include <QInputDialog>
#include <wctype.h>

#include "obs-frontend-api.h"
#include "obs-module.h"
#include "obs.hpp"
#include "version.h"
#include "util/config-file.h"
#include "util/platform.h"

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("Exeldro");
OBS_MODULE_USE_DEFAULT_LOCALE("scene-collection-manager", "en-US")

static obs_hotkey_id sceneCollectionManagerDialog_hotkey_id =
	OBS_INVALID_HOTKEY_ID;
static obs_hotkey_id backup_hotkey_id = OBS_INVALID_HOTKEY_ID;
SceneCollectionManagerDialog *sceneCollectionManagerDialog = nullptr;

static bool autoSaveBackup = false;

void ShowSceneCollectionManagerDialog()
{
	obs_frontend_push_ui_translation(obs_module_get_string);
	if (sceneCollectionManagerDialog == nullptr)
		sceneCollectionManagerDialog = new SceneCollectionManagerDialog(
			static_cast<QMainWindow *>(
				obs_frontend_get_main_window()));
	sceneCollectionManagerDialog->show();
	sceneCollectionManagerDialog->setAttribute(Qt::WA_DeleteOnClose, true);
	QAction::connect(sceneCollectionManagerDialog, &QDialog::finished,
			 [] { sceneCollectionManagerDialog = nullptr; });
	obs_frontend_pop_ui_translation();
}

void SceneCollectionManagerHotkey(void *data, obs_hotkey_id id,
				  obs_hotkey_t *hotkey, bool pressed)
{
	if (!pressed)
		return;

	ShowSceneCollectionManagerDialog();
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

static void BackupSceneCollection()
{
	const auto currentSceneCollection =
		obs_frontend_get_current_scene_collection();
	if (!currentSceneCollection || strlen(currentSceneCollection) < 1)
		return;

	std::string currentSafeName;
	if (!GetFileSafeName(currentSceneCollection, currentSafeName))
		return;

	std::string path = obs_module_config_path("../../basic/scenes/");

	std::string backupName = os_generate_formatted_filename(
		"", true, "%CCYY-%MM-%DD %hh:%mm:%ss");
	backupName.resize(backupName.length() - 1);

	std::string safeName;
	if (!GetFileSafeName(backupName.c_str(), safeName))
		return;

	std::string backupDir = path;
	backupDir += currentSafeName;
	backupDir += "/";
	os_mkdirs(backupDir.c_str());

	std::string filename = path;
	filename += currentSafeName;
	filename += ".json";

	auto *data =
		obs_data_create_from_json_file_safe(filename.c_str(), "bak");
	obs_data_set_string(data, "name", backupName.c_str());
	const auto backupFile = backupDir + safeName + ".json";
	obs_data_save_json(data, backupFile.c_str());
	obs_data_release(data);
}

void BackupSceneCollectionHotkey(void *data, obs_hotkey_id id,
				 obs_hotkey_t *hotkey, bool pressed)
{
	if (!pressed)
		return;

	BackupSceneCollection();
}

std::string GetBackupDirectory(std::string filename)
{

	auto l = filename.length();
	if (filename.compare(l - 5, 5, ".json") == 0) {
		filename.resize(l - 5);
		filename.append("/");
	}
	return filename;
}

static void frontend_event(obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_EXIT) {
		const auto save_data = obs_data_create();
		obs_data_array_t *hotkey_save_array =
			obs_hotkey_save(sceneCollectionManagerDialog_hotkey_id);
		obs_data_set_array(save_data, "sceneCollectionManagerHotkey",
				   hotkey_save_array);
		obs_data_array_release(hotkey_save_array);
		hotkey_save_array = obs_hotkey_save(backup_hotkey_id);
		obs_data_set_array(save_data, "backupHotkey",
				   hotkey_save_array);
		obs_data_array_release(hotkey_save_array);
		auto d = obs_data_get_json(save_data);
		const QByteArray data(d);
		config_set_string(obs_frontend_get_global_config(),
				  "SceneCollectionManager", "HotkeyData",
				  data.toBase64().constData());
		obs_data_release(save_data);
	}
}

static void frontend_save_load(obs_data_t *, bool saving, void *)
{
	if (!saving && autoSaveBackup) {
		BackupSceneCollection();
	}
}

bool obs_module_load()
{
	blog(LOG_INFO, "[Scene Collection Manager] loaded version %s",
	     PROJECT_VERSION);
	const auto action =
		static_cast<QAction *>(obs_frontend_add_tools_menu_qaction(
			obs_module_text("SceneCollectionManager")));

	sceneCollectionManagerDialog_hotkey_id = obs_hotkey_register_frontend(
		"scene_collection_manager",
		obs_module_text("SceneCollectionManager"),
		SceneCollectionManagerHotkey, nullptr);

	backup_hotkey_id = obs_hotkey_register_frontend(
		"backup_scene_collection",
		obs_module_text("BackupSceneCollection"),
		BackupSceneCollectionHotkey, nullptr);

	autoSaveBackup = config_get_bool(obs_frontend_get_global_config(),
					 "SceneCollectionManager",
					 "AutoSaveBackup");
	const auto *data = config_get_string(obs_frontend_get_global_config(),
					     "SceneCollectionManager",
					     "HotkeyData");
	if (data) {
		QByteArray dataBytes = QByteArray::fromBase64(QByteArray(data));
		auto c = dataBytes.constData();
		auto save_data = obs_data_create_from_json(c);
		if (save_data) {
			obs_data_array_t *hotkey_save_array = obs_data_get_array(
				save_data, "sceneCollectionManagerHotkey");
			obs_hotkey_load(sceneCollectionManagerDialog_hotkey_id,
					hotkey_save_array);
			obs_data_array_release(hotkey_save_array);

			hotkey_save_array =
				obs_data_get_array(save_data, "backupHotkey");
			obs_hotkey_load(backup_hotkey_id, hotkey_save_array);
			obs_data_array_release(hotkey_save_array);

			obs_data_release(save_data);
		}
	}
	obs_frontend_add_event_callback(frontend_event, nullptr);
	obs_frontend_add_save_callback(frontend_save_load, nullptr);
	QAction::connect(action, &QAction::triggered,
			 ShowSceneCollectionManagerDialog);
	return true;
}

void obs_module_unload()
{
	obs_frontend_remove_event_callback(frontend_event, nullptr);
	obs_frontend_remove_save_callback(frontend_save_load, nullptr);
	obs_hotkey_unregister(sceneCollectionManagerDialog_hotkey_id);
	obs_hotkey_unregister(backup_hotkey_id);
}

MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("Description");
}

MODULE_EXPORT const char *obs_module_name(void)
{
	return obs_module_text("SceneCollectionManager");
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

void SceneCollectionManagerDialog::on_searchSceneCollectionEdit_textChanged(
	const QString &text)
{
	RefreshSceneCollections();
}

void SceneCollectionManagerDialog::on_actionAddSceneCollection_triggered()
{
	QMenu m;
	auto a = m.addAction(QString::fromUtf8(obs_module_text("New")));
	connect(a, SIGNAL(triggered()), this,
		SLOT(on_actionAddNewSceneCollection_triggered()));
	//m.addAction(QString::fromUtf8(obs_module_text("Import")));
	a = m.addAction(QString::fromUtf8(obs_module_text("Duplicate")));
	connect(a, SIGNAL(triggered()), this,
		SLOT(on_actionDuplicateSceneCollection_triggered()));
	m.exec(QCursor::pos());
}

void SceneCollectionManagerDialog::on_actionAddNewSceneCollection_triggered()
{
	obs_frontend_add_scene_collection("");
}

void SceneCollectionManagerDialog::on_actionDuplicateSceneCollection_triggered()
{
	if (const auto item = ui->sceneCollectionList->currentItem()) {
		const auto filename = scene_collections.at(item->text());
		if (!filename.length())
			return;
		bool ok;
		QString text = QInputDialog::getText(
			this,
			QString::fromUtf8(
				obs_module_text("DuplicateSceneCollection")),
			QString::fromUtf8(obs_module_text("NewName")),
			QLineEdit::Normal, item->text(), &ok);
		if (!ok || text.isEmpty() || text == item->text())
			return;

		std::string safeName;
		if (!GetFileSafeName(text.toUtf8().constData(), safeName))
			return;

		std::string path =
			obs_module_config_path("../../basic/scenes/");

		const auto t = text.toUtf8();
		const auto c = t.constData();
		if (!obs_frontend_add_scene_collection(c))
			return;

		auto *data = obs_data_create_from_json_file_safe(
			filename.c_str(), "bak");
		obs_data_set_string(data, "name", text.toUtf8().constData());

		std::string filePath = path;
		filePath += safeName;
		filePath += ".json";
		obs_data_save_json(data, filePath.c_str());
		obs_data_release(data);

		config_set_string(obs_frontend_get_global_config(), "Basic",
				  "SceneCollection", "");
		config_set_string(obs_frontend_get_global_config(), "Basic",
				  "SceneCollectionFile",
				  "scene_collection_manager_temp");
		obs_frontend_set_current_scene_collection(c);
		std::string tempPath = path;
		tempPath += "scene_collection_manager_temp.json";
		os_unlink(tempPath.c_str());
	}
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
		os_rmdir(GetBackupDirectory(filePath).c_str());
		scene_collections.erase(item->text());
		RefreshSceneCollections();
	}
}

void SceneCollectionManagerDialog::on_actionConfigSceneCollection_triggered()
{
	QMenu m;
	auto a = m.addAction(QString::fromUtf8(obs_module_text("Rename")));
	connect(a, SIGNAL(triggered()), this,
		SLOT(on_actionRenameSceneCollection_triggered()));
	//m.addAction(QString::fromUtf8(obs_module_text("Export")));
	m.exec(QCursor::pos());
}

void SceneCollectionManagerDialog::on_actionRenameSceneCollection_triggered()
{
	if (const auto item = ui->sceneCollectionList->currentItem()) {
		const auto filename = scene_collections.at(item->text());
		if (!filename.length())
			return;
		bool ok;
		QString text = QInputDialog::getText(
			this,
			QString::fromUtf8(
				obs_module_text("RenameSceneCollection")),
			QString::fromUtf8(obs_module_text("NewName")),
			QLineEdit::Normal, item->text(), &ok);
		if (!ok || text.isEmpty() || text == item->text())
			return;

		std::string safeName;
		if (!GetFileSafeName(text.toUtf8().constData(), safeName))
			return;

		std::string path =
			obs_module_config_path("../../basic/scenes/");

		std::string filePath = path;
		filePath += safeName;
		filePath += ".json";

		if (os_file_exists(filePath.c_str()))
			return;

		auto *data = obs_data_create_from_json_file_safe(
			filename.c_str(), "bak");
		auto t = text.toUtf8();
		auto c = t.constData();
		obs_data_set_string(data, "name", c);

		obs_data_save_json(data, filePath.c_str());
		obs_data_release(data);
		os_rename(GetBackupDirectory(filename).c_str(),
			  GetBackupDirectory(filePath).c_str());
		os_unlink(filename.c_str());
		const QString currentSceneCollection = QString::fromUtf8(
			obs_frontend_get_current_scene_collection());
		if (currentSceneCollection == item->text()) {
			config_set_string(obs_frontend_get_global_config(),
					  "Basic", "SceneCollection", c);
			config_set_string(obs_frontend_get_global_config(),
					  "Basic", "SceneCollectionFile",
					  filePath.c_str());
		}
		scene_collections.erase(item->text());
		scene_collections[text] = filePath;
		RefreshSceneCollections();
		const auto items = ui->sceneCollectionList->findItems(
			text, Qt::MatchExactly);
		if (!items.empty()) {
			ui->sceneCollectionList->setCurrentItem(items.at(0));
		}
	}
}

void SceneCollectionManagerDialog::on_actionSwitchSceneCollection_triggered()
{
	if (const auto item = ui->sceneCollectionList->currentItem()) {
		auto t = item->text().toUtf8();
		auto c = t.constData();
		obs_frontend_set_current_scene_collection(c);
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

void SceneCollectionManagerDialog::on_actionConfigBackup_triggered()
{
	QMenu m;
	auto a = m.addAction(QString::fromUtf8(obs_module_text("AutoBackup")));
	a->setCheckable(true);
	a->setChecked(autoSaveBackup);
	connect(a, &QAction::triggered, [] {
		autoSaveBackup = !autoSaveBackup;
		config_set_bool(obs_frontend_get_global_config(),
				"SceneCollectionManager", "AutoSaveBackup",
				autoSaveBackup);
	});
	a = m.addAction(QString::fromUtf8(obs_module_text("Rename")));
	connect(a, SIGNAL(triggered()), this,
		SLOT(on_actionRenameBackup_triggered()));
	m.exec(QCursor::pos());
}

void SceneCollectionManagerDialog::on_actionRenameBackup_triggered()
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

			bool ok;
			QString text = QInputDialog::getText(
				this,
				QString::fromUtf8(
					obs_module_text("RenameBackup")),
				QString::fromUtf8(obs_module_text("NewName")),
				QLineEdit::Normal, backupItem->text(), &ok);
			if (!ok || text.isEmpty() || text == backupItem->text())
				return;

			auto t = text.toUtf8();
			auto c = t.constData();

			std::string newSafeName;
			if (!GetFileSafeName(c, newSafeName))
				return;

			std::string filePath = backupDir;
			filePath += newSafeName;
			filePath += ".json";

			if (os_file_exists(filePath.c_str()))
				return;

			auto *data = obs_data_create_from_json_file(
				backupFile.c_str());

			obs_data_set_string(data, "name", c);

			obs_data_save_json(data, filePath.c_str());
			obs_data_release(data);
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
			const QString currentSceneCollection = QString::fromUtf8(
				obs_frontend_get_current_scene_collection());
			if (currentSceneCollection == item->text()) {
				config_set_string(
					obs_frontend_get_global_config(),
					"Basic", "SceneCollection", "");
				config_set_string(
					obs_frontend_get_global_config(),
					"Basic", "SceneCollectionFile",
					"scene_collection_manager_temp");
				obs_frontend_set_current_scene_collection(
					item->text().toUtf8().constData());
				std::string path = obs_module_config_path(
					"../../basic/scenes/scene_collection_manager_temp.json");
				os_unlink(path.c_str());
			} else {
				obs_frontend_set_current_scene_collection(
					item->text().toUtf8().constData());
			}
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

void SceneCollectionManagerDialog::on_sceneCollectionList_itemDoubleClicked(
	QListWidgetItem *item)
{
	QMetaObject::invokeMethod(this,
				  "on_actionSwitchSceneCollection_triggered",
				  Qt::QueuedConnection);
}

void SceneCollectionManagerDialog::on_backupList_itemDoubleClicked(
	QListWidgetItem *item)
{
	QMetaObject::invokeMethod(this, "on_actionSwitchBackup_triggered",
				  Qt::QueuedConnection);
}

void SceneCollectionManagerDialog::ReadSceneCollections()
{
	std::string path = obs_module_config_path("../../basic/scenes/*.json");
	os_glob_t *glob;
	if (os_glob(path.c_str(), 0, &glob) != 0) {
		blog(LOG_WARNING, "Failed to glob scene collections");
		return;
	}
	scene_collections.clear();

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
		scene_collections[QString::fromUtf8(name.c_str())] = filePath;
	}
}

SceneCollectionManagerDialog::SceneCollectionManagerDialog(QMainWindow *parent)
	: QDialog(parent), ui(new Ui::SceneCollectionManagerDialog)
{
	ui->setupUi(this);

	ReadSceneCollections();
	RefreshSceneCollections();
}

SceneCollectionManagerDialog::~SceneCollectionManagerDialog() {}
