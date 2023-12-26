#include "scene-collection-manager.hpp"

#include <qabstractbutton.h>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QMenu>
#include <QMessageBox>
#include <QInputDialog>
#include <QUrl>
#include <QSpinBox>
#include <QWidgetAction>
#include <wctype.h>
#include <sys/stat.h>

#include "obs-frontend-api.h"
#include "obs-module.h"
#include "obs.hpp"
#include "version.h"
#include "util/config-file.h"
#include "util/platform.h"

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("Exeldro");
OBS_MODULE_USE_DEFAULT_LOCALE("scene-collection-manager", "en-US")

#define MAX_PATH 260

static obs_hotkey_id sceneCollectionManagerDialog_hotkey_id =
	OBS_INVALID_HOTKEY_ID;
static obs_hotkey_id backup_hotkey_id = OBS_INVALID_HOTKEY_ID;
static obs_hotkey_id load_last_backup_hotkey_id = OBS_INVALID_HOTKEY_ID;
static obs_hotkey_id load_first_backup_hotkey_id = OBS_INVALID_HOTKEY_ID;
SceneCollectionManagerDialog *sceneCollectionManagerDialog = nullptr;

static bool autoSaveBackup = false;
static int autoSaveBackupMax = 30;
static std::string customBackupDir;

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
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);
	if (!pressed)
		return;

	ShowSceneCollectionManagerDialog();
}

bool GetFileSafeName(const char *name, std::string &file)
{
	const size_t base_len = strlen(name);
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

	obs_frontend_save();

	std::string currentSafeName;
	if (!GetFileSafeName(currentSceneCollection, currentSafeName))
		return;

	std::string path = obs_module_config_path("../../basic/scenes/");
	std::string path_abs = os_get_abs_path_ptr(path.c_str());
	if (path_abs.back() != '/' && path_abs.back() != '\\') {
		path_abs += "/";
	}

	std::string backupName = os_generate_formatted_filename(
		"", true, "%CCYY-%MM-%DD %hh:%mm:%ss");
	if (backupName[backupName.size() - 1] == '.')
		backupName.resize(backupName.length() - 1);

	std::string safeName;
	if (!GetFileSafeName(backupName.c_str(), safeName))
		return;

	std::string backupDir = path_abs;
	if (!customBackupDir.empty()) {
		backupDir = customBackupDir;
	}
	if (backupDir.back() != '/' && backupDir.back() != '\\') {
		backupDir += "/";
	}
	backupDir += currentSafeName;
	backupDir += "/";
	os_mkdirs(backupDir.c_str());

	std::string filename = path_abs;
	filename += currentSafeName;
	filename += ".json";

	auto *data =
		obs_data_create_from_json_file_safe(filename.c_str(), "bak");
	obs_data_set_string(data, "name", backupName.c_str());
	const auto backupFile = backupDir + safeName + ".json";
	obs_data_save_json(data, backupFile.c_str());
	obs_data_release(data);

	if (!autoSaveBackupMax)
		return;
	int file_count = 0;
	time_t time = 0;
	do {

		const auto f = backupDir + "*.json";
		os_glob_t *glob;
		if (os_glob(f.c_str(), 0, &glob) != 0) {
			return;
		}
		std::string backupFile;
		time = 0;
		file_count = 0;
		for (size_t i = 0; i < glob->gl_pathc; i++) {
			const char *filePath = glob->gl_pathv[i].path;

			if (glob->gl_pathv[i].directory)
				continue;

			const char *fn = filePath;
			const char *slash = strrchr(filePath, '/');
			const char *backslash = strrchr(filePath, '\\');
			if (slash && (!backslash || backslash < slash)) {
				fn = slash + 1;
			} else if (backslash && (!slash || slash < backslash)) {
				fn = backslash + 1;
			}
			int d, t;
			if (sscanf(fn, "%d_%d.json", &d, &t) != 2)
				continue;

			file_count++;
			struct stat stats {};
			if (os_stat(filePath, &stats) == 0 &&
			    stats.st_size > 0 &&
			    (time == 0 || stats.st_ctime <= time)) {
				backupFile = filePath;
				time = stats.st_ctime;
			}
		}
		os_globfree(glob);
		if (time && file_count > autoSaveBackupMax) {
			if (os_unlink(backupFile.c_str()) != 0)
				return;
		}
	} while (time && file_count > autoSaveBackupMax);
}

void BackupSceneCollectionHotkey(void *data, obs_hotkey_id id,
				 obs_hotkey_t *hotkey, bool pressed)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);
	if (!pressed)
		return;

	BackupSceneCollection();
}

std::string GetFilenameFromPath(std::string path, bool with_extension)
{
	const auto slash = path.find_last_of("/\\");
	if (slash != std::string::npos) {
		path = path.substr(slash + 1);
	}
	if (!with_extension) {
		const auto point = path.find_last_of('.');
		if (point != std::string::npos) {
			path = path.substr(0, point);
		}
	}
	return path;
}

std::string GetBackupDirectory(std::string filename)
{
	if (customBackupDir.empty()) {
		auto l = filename.length();
		if (filename.compare(l - 5, 5, ".json") == 0) {
			filename.resize(l - 5);
			filename.append("/");
		}
		return filename;
	}
	filename = GetFilenameFromPath(filename, false);
	std::string dir = customBackupDir;

	if (dir.back() != '/' && dir.back() != '\\')
		dir += "/";
	dir += filename;
	dir += "/";
	return dir;
}

bool activate_dshow_proc(void *p, obs_source_t *source)
{
	if (strcmp(obs_source_get_unversioned_id(source), "dshow_input") != 0)
		return true;
	const bool active = *(bool *)p;
	obs_data_t *settings = obs_source_get_settings(source);
	if (active == obs_data_get_bool(settings, "active")) {
		obs_data_release(settings);
		return true;
	}
	obs_data_release(settings);
	calldata_t cd = {};
	calldata_set_bool(&cd, "active", active);
	proc_handler_t *ph = obs_source_get_proc_handler(source);
	proc_handler_call(ph, "activate", &cd);
	calldata_free(&cd);
	return true;
}

void activate_dshow(bool active)
{
	obs_enum_sources(activate_dshow_proc, &active);
}

void LoadBackupSceneCollection(const std::string sceneCollection,
			       const std::string filename,
			       const std::string backupFile)
{
	if (!filename.length())
		return;

	const auto backupDir = GetBackupDirectory(filename);
	auto *data = obs_data_create_from_json_file(backupFile.c_str());
	obs_data_set_string(data, "name", sceneCollection.c_str());
	obs_data_save_json_safe(data, filename.c_str(), "tmp", "bak");
	obs_data_release(data);
	activate_dshow(false);
	if (strcmp(obs_frontend_get_current_scene_collection(),
		   sceneCollection.c_str()) == 0) {
		config_set_string(obs_frontend_get_global_config(), "Basic",
				  "SceneCollection", "");
		config_set_string(obs_frontend_get_global_config(), "Basic",
				  "SceneCollectionFile",
				  "scene_collection_manager_temp");
		obs_frontend_set_current_scene_collection(
			sceneCollection.c_str());
		std::string path = obs_module_config_path(
			"../../basic/scenes/scene_collection_manager_temp.json");
		std::string path_abs = os_get_abs_path_ptr(path.c_str());
		os_unlink(path_abs.c_str());
	} else {
		obs_frontend_set_current_scene_collection(
			sceneCollection.c_str());
	}
	activate_dshow(true);
}

void LoadBackupSceneCollection(bool last)
{
	const auto config = obs_frontend_get_global_config();
	if (!config)
		return;
	const std::string sceneCollection =
		config_get_string(config, "Basic", "SceneCollection");
	const std::string filename =
		config_get_string(config, "Basic", "SceneCollectionFile");
	std::string path = obs_module_config_path("../../basic/scenes/");
	path += filename;
	path += ".json";
	std::string path_abs = os_get_abs_path_ptr(path.c_str());

	const auto backupDir = GetBackupDirectory(path_abs);
	const auto f = backupDir + "*.json";
	os_glob_t *glob;
	if (os_glob(f.c_str(), 0, &glob) != 0) {
		return;
	}
	std::string backupFile;
	time_t time = 0;
	for (size_t i = 0; i < glob->gl_pathc; i++) {
		const char *filePath = glob->gl_pathv[i].path;

		if (glob->gl_pathv[i].directory)
			continue;

		struct stat stats {};
		if (os_stat(filePath, &stats) == 0 && stats.st_size > 0) {
			if (last) {
				if (time == 0 || stats.st_ctime >= time) {
					backupFile = filePath;
					time = stats.st_ctime;
				}
			} else {
				if (time == 0 || stats.st_ctime <= time) {
					backupFile = filePath;
					time = stats.st_ctime;
				}
			}
		}
	}
	os_globfree(glob);
	if (time != 0)
		LoadBackupSceneCollection(sceneCollection, path_abs,
					  backupFile);
}

void LoadLastBackupSceneCollectionHotkey(void *data, obs_hotkey_id id,
					 obs_hotkey_t *hotkey, bool pressed)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);
	if (!pressed)
		return;
	const auto main =
		static_cast<QMainWindow *>(obs_frontend_get_main_window());
	QMetaObject::invokeMethod(
		main, [] { LoadBackupSceneCollection(true); },
		Qt::QueuedConnection);
}

void LoadFirstBackupSceneCollectionHotkey(void *data, obs_hotkey_id id,
					  obs_hotkey_t *hotkey, bool pressed)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);
	if (!pressed)
		return;
	const auto main =
		static_cast<QMainWindow *>(obs_frontend_get_main_window());
	QMetaObject::invokeMethod(
		main, [] { LoadBackupSceneCollection(false); },
		Qt::QueuedConnection);
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
		hotkey_save_array = obs_hotkey_save(load_last_backup_hotkey_id);
		obs_data_set_array(save_data, "loadLastBackupHotkey",
				   hotkey_save_array);
		obs_data_array_release(hotkey_save_array);
		hotkey_save_array =
			obs_hotkey_save(load_first_backup_hotkey_id);
		obs_data_set_array(save_data, "loadFirstBackupHotkey",
				   hotkey_save_array);
		obs_data_array_release(hotkey_save_array);
		auto d = obs_data_get_json(save_data);
		const QByteArray data(d);
		config_set_string(obs_frontend_get_global_config(),
				  "SceneCollectionManager", "HotkeyData",
				  data.toBase64().constData());
		obs_data_release(save_data);
	} else if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED) {
		activate_dshow(true);
	}
}

static void frontend_save_load(obs_data_t *, bool saving, void *)
{
	if (!saving) {
		if (autoSaveBackup)
			BackupSceneCollection();
		activate_dshow(true);
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

	load_last_backup_hotkey_id = obs_hotkey_register_frontend(
		"load_last_backup_scene_collection",
		obs_module_text("LoadLastBackupSceneCollection"),
		LoadLastBackupSceneCollectionHotkey, nullptr);

	load_first_backup_hotkey_id = obs_hotkey_register_frontend(
		"load_first_backup_scene_collection",
		obs_module_text("LoadFirstBackupSceneCollection"),
		LoadFirstBackupSceneCollectionHotkey, nullptr);

	auto config = obs_frontend_get_global_config();
	autoSaveBackup = config_get_bool(config, "SceneCollectionManager",
					 "AutoSaveBackup");
	autoSaveBackupMax = (int)config_get_int(
		config, "SceneCollectionManager", "AutoSaveBackupMax");
	auto *d = config_get_string(config, "SceneCollectionManager",
				    "BackupDir");
	if (d)
		customBackupDir = d;
	const auto *data = config_get_string(config, "SceneCollectionManager",
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

			hotkey_save_array = obs_data_get_array(
				save_data, "loadLastBackupHotkey");
			obs_hotkey_load(load_last_backup_hotkey_id,
					hotkey_save_array);
			obs_data_array_release(hotkey_save_array);

			hotkey_save_array = obs_data_get_array(
				save_data, "loadFirstBackupHotkey");
			obs_hotkey_load(load_first_backup_hotkey_id,
					hotkey_save_array);
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
	obs_hotkey_unregister(load_first_backup_hotkey_id);
	obs_hotkey_unregister(load_last_backup_hotkey_id);
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
			item->setSelected(true);
			ui->sceneCollectionList->setCurrentItem(item);
		}
	}
}

void SceneCollectionManagerDialog::on_searchSceneCollectionEdit_textChanged(
	const QString &text)
{
	UNUSED_PARAMETER(text);
	RefreshSceneCollections();
}

void SceneCollectionManagerDialog::on_actionAddSceneCollection_triggered()
{
	QMenu m;
	auto a = m.addAction(QString::fromUtf8(obs_module_text("New")));
	connect(a, SIGNAL(triggered()), this,
		SLOT(on_actionAddNewSceneCollection_triggered()));
	a = m.addAction(QString::fromUtf8(obs_module_text("Import")));
	connect(a, SIGNAL(triggered()), this,
		SLOT(on_actionImportSceneCollection_triggered()));
	a = m.addAction(QString::fromUtf8(obs_module_text("Duplicate")));
	connect(a, SIGNAL(triggered()), this,
		SLOT(on_actionDuplicateSceneCollection_triggered()));
	m.exec(QCursor::pos());
}

void SceneCollectionManagerDialog::on_actionAddNewSceneCollection_triggered()
{
	obs_frontend_add_scene_collection("");
}

void SceneCollectionManagerDialog::on_actionImportSceneCollection_triggered()
{
	auto files = QFileDialog::getOpenFileNames(
		this, obs_module_text("ImportSceneCollection"), "",
		"Scene Collection (*.json)");
	if (files.isEmpty())
		return;
	char path_buffer[MAX_PATH];
	auto scene_collections_ = scene_collections;
	for (auto file : files) {

		auto fu = file.toUtf8();

		auto data = obs_data_create_from_json_file(fu.constData());
		if (!data)
			continue;

		auto name = obs_data_get_string(data, "name");
		auto n = QString::fromUtf8(name);

		bool replace_current = false;
		if (scene_collections_.count(n) > 0) {
			//TODO ask if replace
			auto sc = obs_frontend_get_current_scene_collection();
			if (strcmp(sc, name) == 0) {
				replace_current = true;
			} else {
			}
		} else {
			if (!obs_frontend_add_scene_collection(name)) {
				obs_data_release(data);
				continue;
			}
			replace_current = true;
		}
		std::string safeName;
		if (!GetFileSafeName(name, safeName)) {
			obs_data_release(data);
			continue;
		}
		std::string dir = fu.constData();
		std::size_t slash = dir.find_last_of("/\\");
		if (slash != std::string::npos) {
			auto point = dir.find_last_of('.');
			if (point != std::string::npos && point > slash) {
				dir = dir.substr(0, point);
				dir += "/";
				try_fix_paths(data, dir.c_str(), path_buffer);
			}
			dir = dir.substr(0, slash + 1);
		}
		import_parts(data, dir.c_str());
		try_fix_paths(data, dir.c_str(), path_buffer);
		replace_os_specific(data);

		std::string path =
			obs_module_config_path("../../basic/scenes/");
		path += safeName;
		path += ".json";
		std::string path_abs = os_get_abs_path_ptr(path.c_str());
		obs_data_save_json_safe(data, path_abs.c_str(), "tmp", "bak");

		if (replace_current) {
			config_set_string(obs_frontend_get_global_config(),
					  "Basic", "SceneCollection", "");
			config_set_string(obs_frontend_get_global_config(),
					  "Basic", "SceneCollectionFile",
					  "scene_collection_manager_temp");

			obs_frontend_set_current_scene_collection(name);
			std::string tempPath = ""; //path;
			tempPath += "scene_collection_manager_temp.json";
			os_unlink(tempPath.c_str());
		}
		obs_data_release(data);
		BackupSceneCollection();
	}
}

static bool replace(std::string &str, const char *from, const char *to)
{
	size_t start_pos = str.find(from);
	if (start_pos == std::string::npos)
		return false;
	str.replace(start_pos, strlen(from), to);
	return true;
}

void SceneCollectionManagerDialog::import_parts(obs_data_t *data,
						const char *dir)
{
	obs_data_array_t *a = obs_data_get_array(data, "imports");
	if (!a)
		return;
	size_t count = obs_data_array_count(a);
	for (size_t i = 0; i < count; i++) {
		obs_data_t *item = obs_data_array_item(a, i);
		if (!item)
			continue;
		const char *file = obs_data_get_string(item, "file");
		obs_data_t *file_data = nullptr;
		if (!file || !strlen(file)) {
			obs_data_release(item);
			continue;
		}
		if (os_file_exists(file)) {
			file_data = obs_data_create_from_json_file(file);
		}
		if (!file_data) {
			std::string newFile = dir;
			newFile += file;
			if (os_file_exists(newFile.c_str())) {
				file_data = obs_data_create_from_json_file(
					newFile.c_str());
			}
		}
		if (!file_data) {
			obs_data_release(item);
			continue;
		}
		obs_data_item_t *item2 = obs_data_first(file_data);
		while (item2) {
			if (obs_data_item_gettype(item2) != OBS_DATA_ARRAY) {
				obs_data_item_next(&item2);
				continue;
			}
			obs_data_array_t *fa = obs_data_item_get_array(item2);
			obs_data_array_t *da = obs_data_get_array(
				data, obs_data_item_get_name(item2));
			if (!da) {
				da = obs_data_array_create();
				obs_data_set_array(
					data, obs_data_item_get_name(item2),
					da);
			}
			size_t c = obs_data_array_count(fa);
			for (size_t j = 0; j < c; j++) {
				obs_data_t *fi = obs_data_array_item(fa, j);
				if (!fi)
					continue;
				const char *name =
					obs_data_get_string(fi, "name");
				if (!name || !strlen(name)) {
					obs_data_release(fi);
					continue;
				}
				bool found = false;
				size_t c2 = obs_data_array_count(da);
				for (size_t k = 0; k < c2; k++) {
					obs_data_t *di =
						obs_data_array_item(da, k);
					if (!di)
						continue;
					if (strcmp(obs_data_get_string(di,
								       "name"),
						   name) == 0) {
						obs_data_array_erase(da, k);
						obs_data_array_insert(da, k,
								      fi);
						found = true;
						break;
					}

					obs_data_release(di);
				}
				if (!found) {
					obs_data_array_push_back(da, fi);
				}
				obs_data_release(fi);
			}
			obs_data_item_next(&item2);
		}
		obs_data_release(file_data);
		obs_data_release(item);
	}
}

void SceneCollectionManagerDialog::try_fix_paths(obs_data_t *data,
						 const char *dir,
						 char *path_buffer)
{
	obs_data_item_t *item = obs_data_first(data);
	while (item) {
		const enum obs_data_type type = obs_data_item_gettype(item);
		if (type == OBS_DATA_STRING) {
			std::string str = obs_data_item_get_string(item);
			bool edit = false;
			if (replace(str, "[U_COMBOBULATOR_PATH]", dir)) {
				obs_data_item_set_string(&item, str.c_str());
				edit = true;
			}
			bool local_url = false;
			if (str.substr(0, 7) == "file://") {
				str = str.substr(7);
				local_url = true;
			}
			std::size_t found = str.find_last_of("/\\");
			if (str.length() < MAX_PATH &&
			    found != std::string::npos &&
			    !os_file_exists(str.c_str())) {
				while (found != std::string::npos) {
					auto file =
						found == 0 && str[0] != '/' &&
								str[0] != '\\'
							? str
							: str.substr(found + 1);
					if (file.find('.') == std::string::npos)
						break;
					auto oldDir = str.substr(0, found + 1);
					std::string newFile = dir;
					newFile += file;
					if (os_file_exists(newFile.c_str())) {
						if (local_url) {
							str = "file://";
							if (os_get_abs_path(
								    newFile.c_str(),
								    path_buffer,
								    MAX_PATH)) {
								for (auto i = 0;
								     path_buffer
									     [i] !=
								     '\0';
								     i++)
									if (path_buffer
										    [i] ==
									    '\\')
										path_buffer[i] =
											'/';
								str += path_buffer;
							}
						} else {
							if (os_get_abs_path(
								    newFile.c_str(),
								    path_buffer,
								    MAX_PATH)) {
								for (auto i = 0;
								     path_buffer
									     [i] !=
								     '\0';
								     i++)
									if (path_buffer
										    [i] ==
									    '\\')
										path_buffer[i] =
											'/';
								str = path_buffer;
							} else {
								str = "";
							}
						}
						obs_data_item_set_string(
							&item, str.c_str());
						edit = true;
						break;
					}
					if (found == 0) {
						found = std::string::npos;
					} else {
						found = str.find_last_of(
							"/\\", found - 1);
						if (found ==
						    std::string::npos) {
							found = 0;
						}
					}
				}
			}
			if (edit) {
				item = obs_data_first(data);
				continue;
			}
		} else if (type == OBS_DATA_OBJECT) {
			if (obs_data_t *obj = obs_data_item_get_obj(item)) {
				try_fix_paths(obj, dir, path_buffer);
				obs_data_release(obj);
			}
		} else if (type == OBS_DATA_ARRAY) {
			const auto array = obs_data_item_get_array(item);
			const auto count = obs_data_array_count(array);
			for (size_t i = 0; i < count; i++) {
				if (obs_data_t *obj =
					    obs_data_array_item(array, i)) {
					try_fix_paths(obj, dir, path_buffer);
					obs_data_release(obj);
				}
			}
		}
		obs_data_item_next(&item);
	}
}

bool SceneCollectionManagerDialog::replace_source(obs_data_t *s, const char *id,
						  const char *find,
						  const char *replace, bool cs)
{
	if (strcmp(id, find) != 0)
		return false;
	obs_data_set_string(s, "id", replace);
	const char *vid = obs_get_latest_input_type_id(replace);
	obs_data_set_string(s, "versioned_id", vid ? vid : id);
	if (cs) {
		obs_data_t *c = obs_data_create();
		obs_data_set_obj(s, "settings", c);
		obs_data_release(c);
	}

	return true;
}

void SceneCollectionManagerDialog::make_source_mac(obs_data_t *s)
{
	char *id = bstrdup(obs_data_get_string(s, "id"));
	replace_source(s, id, "game_capture", "syphon-input");
	replace_source(s, id, "wasapi_input_capture",
		       "coreaudio_input_capture");
	replace_source(s, id, "wasapi_output_capture",
		       "coreaudio_output_capture");
	replace_source(s, id, "pulse_input_capture", "coreaudio_input_capture");
	replace_source(s, id, "pulse_output_capture",
		       "coreaudio_output_capture");
	replace_source(s, id, "jack_output_capture",
		       "coreaudio_output_capture");
	replace_source(s, id, "alsa_input_capture", "coreaudio_input_capture");
	replace_source(s, id, "dshow_input", "av_capture_input");
	replace_source(s, id, "v4l2_input", "av_capture_input");
	replace_source(s, id, "xcomposite_input", "window_capture");
	replace_source(s, id, "xshm_input", "monitor_capture", false);
	bfree(id);
}
void SceneCollectionManagerDialog::make_source_windows(obs_data_t *s)
{
	char *id = bstrdup(obs_data_get_string(s, "id"));
	replace_source(s, id, "syphon-input", "game_capture");
	replace_source(s, id, "coreaudio_input_capture",
		       "wasapi_input_capture");
	replace_source(s, id, "coreaudio_output_capture",
		       "wasapi_output_capture");
	replace_source(s, id, "pulse_input_capture", "wasapi_input_capture");
	replace_source(s, id, "pulse_output_capture", "wasapi_output_capture");
	replace_source(s, id, "jack_output_capture", "wasapi_output_capture");
	replace_source(s, id, "alsa_input_capture", "wasapi_input_capture");
	replace_source(s, id, "av_capture_input", "dshow_input");
	replace_source(s, id, "v4l2_input", "dshow_input");
	replace_source(s, id, "xcomposite_input", "window_capture");
	bfree(id);
}
void SceneCollectionManagerDialog::make_source_linux(obs_data_t *s)
{
	char *id = bstrdup(obs_data_get_string(s, "id"));
	replace_source(s, id, "coreaudio_input_capture", "pulse_input_capture");
	replace_source(s, id, "coreaudio_output_capture",
		       "pulse_output_capture");
	replace_source(s, id, "wasapi_input_capture", "pulse_input_capture");
	replace_source(s, id, "wasapi_output_capture", "pulse_output_capture");
	replace_source(s, id, "av_capture_input", "v4l2_input");
	replace_source(s, id, "dshow_input", "v4l2_input");
	replace_source(s, id, "window_capture", "xcomposite_input");
	bfree(id);
}

void SceneCollectionManagerDialog::replace_gdi_sceneitem_transform(
	obs_data_t *item, std::map<std::string, obs_data_t *> gdi_sources)
{
	const char *name = obs_data_get_string(item, "name");
	auto gdi = gdi_sources.find(name);
	if (gdi == gdi_sources.end())
		return;
	struct vec2 scale;
	obs_data_get_vec2(item, "scale", &scale);
	scale.x *= 9.0f / 11.0f;
	scale.y *= 9.0f / 11.0f;
	if (obs_data_get_bool(gdi->second, "extents_wrap")) {
		obs_data_set_int(item, "bounds_type", OBS_BOUNDS_MAX_ONLY);
		struct vec2 bounds;
		bounds.x = obs_data_get_double(gdi->second, "extents_cx");
		bounds.y = obs_data_get_double(gdi->second, "extents_cy");
		if (bounds.y < 2.0) {
			auto font = obs_data_get_obj(gdi->second, "font");
			auto font_size = obs_data_get_double(font, "size");
			obs_data_release(font);
			bounds.y = font_size;
		}
		obs_data_set_vec2(item, "bounds", &bounds);
	} else {
		obs_data_set_vec2(item, "scale", &scale);
	}
	const char *align_str = obs_data_get_string(gdi->second, "align");
	const char *valign_str = obs_data_get_string(gdi->second, "valign");
	int bounds_align = 0;
	if (strcmp(align_str, "center") == 0) {
		bounds_align += OBS_ALIGN_CENTER;
		obs_data_set_int(gdi->second, "custom_width", 0);
	} else if (strcmp(align_str, "right") == 0) {

		bounds_align += OBS_ALIGN_RIGHT;
	} else {

		bounds_align += OBS_ALIGN_LEFT;
	}

	if (strcmp(valign_str, "center") == 0)
		bounds_align += OBS_ALIGN_CENTER;
	else if (strcmp(valign_str, "bottom") == 0)
		bounds_align += OBS_ALIGN_BOTTOM;
	else
		bounds_align += OBS_ALIGN_TOP;
	obs_data_set_int(item, "bounds_align", bounds_align);
}

void SceneCollectionManagerDialog::replace_os_specific(obs_data_t *data)
{
	obs_data_array_t *sources = obs_data_get_array(data, "sources");
	if (!sources)
		return;
	std::map<std::string, obs_data_t *> gdi_sources;
	auto count = obs_data_array_count(sources);
	for (size_t i = 0; i < count; i++) {
		obs_data_t *s = obs_data_array_item(sources, i);
		if (!s)
			continue;
#ifdef WIN32
		make_source_windows(s);
#elif __APPLE__
		make_source_mac(s);
#else
		make_source_linux(s);
#endif
#ifndef WIN32
		if (strcmp(obs_data_get_string(s, "id"), "text_gdiplus") == 0) {
			obs_data_set_string(s, "id", "text_ft2_source");
			obs_data_set_string(s, "versioned_id",
					    "text_ft2_source_v2");
			obs_data_t *settings = obs_data_get_obj(s, "settings");
			if (settings) {
				obs_data_set_default_int(settings, "color",
							 0xFFFFFF);
				long long color =
					obs_data_get_int(settings, "color");
				color = color & 0xFFFFFF;
				obs_data_set_default_int(settings, "opacity",
							 100);
				long long opacity =
					obs_data_get_int(settings, "opacity");
				color |= ((opacity * 255 / 100) & 0xFF) << 24;
				obs_data_set_int(settings, "color1", color);
				obs_data_set_int(settings, "color2", color);
				obs_data_set_default_bool(settings,
							  "extents_wrap", true);
				if (obs_data_get_bool(settings,
						      "extents_wrap")) {
					obs_data_set_default_int(
						settings, "extents_cx", 100);
					obs_data_set_int(
						settings, "custom_width",
						obs_data_get_int(settings,
								 "extents_cx"));
					obs_data_set_bool(settings, "word_wrap",
							  true);
				}
				gdi_sources.emplace(obs_data_get_string(s,
									"name"),
						    settings);
			}
		}
#endif
		obs_data_release(s);
	}
#ifndef WIN32
	for (size_t i = 0; i < count; i++) {
		obs_data_t *s = obs_data_array_item(sources, i);
		if (!s)
			continue;
		if (strcmp(obs_data_get_string(s, "id"), "scene") != 0 &&
		    strcmp(obs_data_get_string(s, "id"), "group") != 0) {
			obs_data_release(s);
			continue;
		}

		obs_data_t *settings = obs_data_get_obj(s, "settings");
		obs_data_array_t *items = obs_data_get_array(settings, "items");
		obs_data_release(settings);
		auto item_count = obs_data_array_count(items);
		for (size_t j = 0; j < item_count; j++) {
			obs_data_t *item = obs_data_array_item(items, j);
			if (!item)
				continue;
			replace_gdi_sceneitem_transform(item, gdi_sources);
			obs_data_release(item);
		}
		obs_data_array_release(items);
		obs_data_release(s);
	}
	obs_data_array_release(sources);

	sources = obs_data_get_array(data, "groups");
	count = obs_data_array_count(sources);
	for (size_t i = 0; i < count; i++) {
		obs_data_t *s = obs_data_array_item(sources, i);
		if (!s)
			continue;
		obs_data_t *settings = obs_data_get_obj(s, "settings");
		obs_data_array_t *items = obs_data_get_array(settings, "items");
		obs_data_release(settings);
		auto item_count = obs_data_array_count(items);
		for (size_t j = 0; j < item_count; j++) {
			obs_data_t *item = obs_data_array_item(items, j);
			if (!item)
				continue;
			replace_gdi_sceneitem_transform(item, gdi_sources);
			obs_data_release(item);
		}
		obs_data_array_release(items);
		obs_data_release(s);
	}
	obs_data_array_release(sources);
#endif
	for (const auto &kv : gdi_sources) {
		obs_data_release(kv.second);
	}
	auto globalAudio = {"DesktopAudioDevice1", "DesktopAudioDevice2",
			    "AuxAudioDevice1",     "AuxAudioDevice2",
			    "AuxAudioDevice3",     "AuxAudioDevice4"};
	for (auto ga : globalAudio) {
		obs_data_t *s = obs_data_get_obj(data, ga);
		if (!s)
			continue;
#ifdef WIN32
		make_source_windows(s);
#elif __APPLE__
		make_source_mac(s);
#else
		make_source_linux(s);
#endif
		obs_data_release(s);
	}
}

void SceneCollectionManagerDialog::export_local_files(obs_data_t *data,
						      std::string dir,
						      std::string subdir)
{
	obs_data_item_t *item = obs_data_first(data);
	while (item) {
		const enum obs_data_type type = obs_data_item_gettype(item);
		if (type == OBS_DATA_STRING) {
			std::string str = obs_data_item_get_string(item);
			bool local_url = false;
			if (str.substr(0, 7) == "file://") {
				str = str.substr(7);
				local_url = true;
			}
			auto slash = str.find('\\');
			while (slash != std::string::npos) {
				str.replace(slash, slash + 1, "/");
				slash = str.find('\\');
			}
			std::size_t found = str.find_last_of("/\\");
			if (found != std::string::npos &&
			    os_file_exists(str.c_str()) &&
			    (str.length() < dir.length() ||
			     str.substr(0, dir.length()) != dir)) {
				//todo move file to dir and update item
				auto filename = GetFilenameFromPath(str, true);
				std::string newDir = subdir;
				std::string safe;
				auto n = obs_data_item_get_name(item);
				if (n && GetFileSafeName(n, safe)) {
					newDir += safe;
					newDir += "/";
				}
				auto fullNewDir = dir + newDir;
				os_mkdirs(fullNewDir.c_str());
				auto newFile = fullNewDir + filename;
				if (os_file_exists(newFile.c_str())) {
					os_unlink(newFile.c_str());
				}
				if (os_copyfile(str.c_str(), newFile.c_str()) ==
				    0) {
					newFile = newDir + filename;
					if (local_url) {
						str = "file://";
						str += newFile.c_str();
					} else {
						str = newFile.c_str();
					}
					obs_data_item_set_string(&item,
								 str.c_str());
					item = obs_data_first(data);
					continue;
				}
			}
		} else if (type == OBS_DATA_OBJECT) {
			if (obs_data_t *obj = obs_data_item_get_obj(item)) {
				std::string newDir = subdir;
				std::string safe;
				auto n = obs_data_item_get_name(item);
				if (n && GetFileSafeName(n, safe)) {
					newDir += safe;
					newDir += "/";
				}
				export_local_files(obj, dir, newDir);
				obs_data_release(obj);
			}
		} else if (type == OBS_DATA_ARRAY) {
			std::string newDir = dir;
			newDir += obs_data_item_get_name(item);
			newDir += "/";
			const auto array = obs_data_item_get_array(item);
			const auto count = obs_data_array_count(array);
			for (size_t i = 0; i < count; i++) {
				if (obs_data_t *obj =
					    obs_data_array_item(array, i)) {
					std::string newDir = subdir;
					std::string safe;
					auto n = obs_data_item_get_name(item);
					if (n && GetFileSafeName(n, safe)) {
						newDir += safe;
						newDir += "/";
					}
					n = obs_data_get_string(obj, "name");
					if (n && GetFileSafeName(n, safe)) {
						newDir += safe;
						newDir += "/";
					}
					export_local_files(obj, dir, newDir);
					obs_data_release(obj);
				}
			}
		}
		obs_data_item_next(&item);
	}
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
		std::string path_abs = os_get_abs_path_ptr(path.c_str());
		if (path_abs.back() != '/' && path_abs.back() != '\\') {
			path_abs += "/";
		}

		const auto t = text.toUtf8();
		const auto c = t.constData();
		if (!obs_frontend_add_scene_collection(c))
			return;

		auto *data = obs_data_create_from_json_file_safe(
			filename.c_str(), "bak");
		obs_data_set_string(data, "name", text.toUtf8().constData());

		std::string filePath = path_abs;
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
		std::string tempPath = path_abs;
		tempPath += "scene_collection_manager_temp.json";
		os_unlink(tempPath.c_str());
	}
}

void SceneCollectionManagerDialog::on_actionRemoveSceneCollection_triggered()
{
	auto items = ui->sceneCollectionList->selectedItems();
	if (items.isEmpty()) {
		if (const auto item = ui->sceneCollectionList->currentItem())
			items.append(item);
		else
			return;
	}
	QMessageBox remove(this);
	remove.setText(QString::fromUtf8(
		obs_module_text("DoYouWantToRemoveSceneCollection")));
	QPushButton *yes =
		remove.addButton(QString::fromUtf8(obs_module_text("Yes")),
				 QMessageBox::YesRole);
	remove.setDefaultButton(yes);
	remove.addButton(QString::fromUtf8(obs_module_text("No")),
			 QMessageBox::NoRole);
	remove.setIcon(QMessageBox::Question);
	remove.setWindowTitle(
		QString::fromUtf8(obs_module_text("ConfirmRemove")));
	remove.exec();

	if (reinterpret_cast<QAbstractButton *>(yes) != remove.clickedButton())
		return;
	for (auto &item : items) {
		auto filePath = scene_collections.at(item->text());
		if (filePath.length() == 0)
			continue;
		filePath = os_get_abs_path_ptr(filePath.c_str());
		os_unlink(filePath.c_str());
		auto backupDir = GetBackupDirectory(filePath);
		const auto f = backupDir + "*.json";
		os_glob_t *glob;
		if (os_glob(f.c_str(), 0, &glob) == 0) {
			for (size_t i = 0; i < glob->gl_pathc; i++) {
				const char *backupFilePath =
					glob->gl_pathv[i].path;

				if (glob->gl_pathv[i].directory)
					continue;
				os_unlink(backupFilePath);
			}
			os_globfree(glob);
		}
		os_rmdir(backupDir.c_str());
		scene_collections.erase(item->text());
	}
	RefreshSceneCollections();
}

void SceneCollectionManagerDialog::on_actionConfigSceneCollection_triggered()
{
	QMenu m;
	auto a = m.addAction(QString::fromUtf8(obs_module_text("Rename")));
	connect(a, SIGNAL(triggered()), this,
		SLOT(on_actionRenameSceneCollection_triggered()));
	a = m.addAction(QString::fromUtf8(obs_module_text("Export")));
	connect(a, SIGNAL(triggered()), this,
		SLOT(on_actionExportSceneCollection_triggered()));
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
		std::string path_abs = os_get_abs_path_ptr(path.c_str());
		if (path_abs.back() != '/' && path_abs.back() != '\\') {
			path_abs += "/";
		}

		std::string filePath = path_abs;
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

void SceneCollectionManagerDialog::on_actionExportSceneCollection_triggered()
{
	const auto item = ui->sceneCollectionList->currentItem();
	if (!item)
		return;
	const auto filename = scene_collections.at(item->text());
	if (!filename.length())
		return;
	const QString file = QFileDialog::getSaveFileName(
		this, obs_module_text("ExportSceneCollection"), "",
		"Scene Collection (*.json)");
	if (file.isEmpty())
		return;

	auto data =
		obs_data_create_from_json_file_safe(filename.c_str(), "bak");
	auto f = file.toUtf8();
	std::string dir = f.constData();
	auto slash = dir.find_last_of("/\\");
	if (slash != std::string::npos) {
		auto point = dir.find_last_of('.');
		if (point != std::string::npos && point > slash) {
			dir = dir.substr(0, point);
			dir += "/";
		} else {
			dir = dir.substr(0, slash + 1);
		}
	}
	slash = dir.find('\\');
	while (slash != std::string::npos) {
		dir.replace(slash, slash + 1, "/");
		slash = dir.find('\\');
	}
	export_local_files(data, dir, "");
	obs_data_save_json(data, f.constData());
	obs_data_release(data);
}

void SceneCollectionManagerDialog::on_actionSwitchSceneCollection_triggered()
{
	if (const auto item = ui->sceneCollectionList->currentItem()) {
		auto t = item->text().toUtf8();
		auto c = t.constData();
		activate_dshow(false);
		obs_frontend_set_current_scene_collection(c);
		activate_dshow(true);
	}
}

void SceneCollectionManagerDialog::on_actionAddBackup_triggered()
{
	if (const auto item = ui->sceneCollectionList->currentItem()) {
		const auto filename = scene_collections.at(item->text());
		if (!filename.length())
			return;

		const auto currentSceneCollection =
			obs_frontend_get_current_scene_collection();
		if (currentSceneCollection &&
		    strlen(currentSceneCollection) > 0 &&
		    item->text() == QString::fromUtf8(currentSceneCollection)) {
			obs_frontend_save();
		}
		bfree(currentSceneCollection);

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

		auto backupItems = ui->backupList->selectedItems();
		if (backupItems.isEmpty()) {
			if (const auto backupItem =
				    ui->backupList->currentItem())
				backupItems.append(backupItem);
			else
				return;
		}
		QMessageBox remove(this);
		remove.setText(QString::fromUtf8(
			obs_module_text("DoYouWantToRemoveBackup")));
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
		for (auto &backupItem : backupItems) {
			const auto backupDir = GetBackupDirectory(filename);
			auto itemName = backupItem->text();
			auto itemNameUtf8 = itemName.toUtf8();
			std::string safeName;
			if (!GetFileSafeName(itemNameUtf8.constData(),
					     safeName))
				continue;

			const auto backupFile = backupDir + safeName + ".json";
			os_unlink(backupFile.c_str());
		}
		on_sceneCollectionList_currentRowChanged(
			ui->sceneCollectionList->currentRow());
	}
}

void SceneCollectionManagerDialog::on_actionConfigBackup_triggered()
{
	QMenu m;
	auto a = m.addAction(QString::fromUtf8(obs_module_text("Rename")));
	connect(a, SIGNAL(triggered()), this,
		SLOT(on_actionRenameBackup_triggered()));
	m.addSeparator();

	a = m.addAction(QString::fromUtf8(obs_module_text("AutoBackup")));
	a->setCheckable(true);
	a->setChecked(autoSaveBackup);
	connect(a, &QAction::triggered, [] {
		autoSaveBackup = !autoSaveBackup;
		const auto config = obs_frontend_get_global_config();
		config_set_bool(config, "SceneCollectionManager",
				"AutoSaveBackup", autoSaveBackup);
	});

	QWidget *maxRow = new QWidget(&m);
	auto hl = new QHBoxLayout;
	maxRow->setLayout(hl);

	QSpinBox *maxSpin = new QSpinBox(&m);
	maxSpin->setMinimum(0);
	maxSpin->setMaximum(1000);
	maxSpin->setSingleStep(1);
	maxSpin->setValue(autoSaveBackupMax);

	hl->addWidget(maxSpin);

	QWidgetAction *maxAction = new QWidgetAction(&m);
	maxAction->setDefaultWidget(maxRow);

	connect(maxSpin, (void(QSpinBox::*)(int)) & QSpinBox::valueChanged,
		[](int val) {
			autoSaveBackupMax = val;
			const auto config = obs_frontend_get_global_config();
			config_set_int(config, "SceneCollectionManager",
				       "AutoSaveBackupMax", autoSaveBackupMax);
		});

	m.addMenu(QString::fromUtf8(obs_module_text("Max")))
		->addAction(maxAction);

	m.addSeparator();

	auto dirMenu =
		m.addMenu(QString::fromUtf8(obs_module_text("BackupDir")));
	a = dirMenu->addAction(QString::fromUtf8(obs_module_text("ShowDir")));
	connect(a, &QAction::triggered, [] {
		QUrl url;
		if (customBackupDir.empty()) {
			auto ptr = os_get_abs_path_ptr(
				obs_module_config_path("../../basic/scenes/"));
			url = QUrl::fromLocalFile(QString::fromUtf8(ptr));
			bfree(ptr);
		} else {
			url = QUrl::fromLocalFile(
				QString::fromUtf8(customBackupDir.c_str()));
		}
		QDesktopServices::openUrl(url);
	});

	dirMenu->addSeparator();
	a = dirMenu->addAction(QString::fromUtf8(obs_module_text("Default")));
	a->setCheckable(true);
	a->setChecked(customBackupDir.empty());
	connect(a, &QAction::triggered, [this] {
		customBackupDir = "";
		const auto config = obs_frontend_get_global_config();
		config_set_string(config, "SceneCollectionManager", "BackupDir",
				  customBackupDir.c_str());
		on_sceneCollectionList_currentRowChanged(
			ui->sceneCollectionList->currentRow());
	});
	a = dirMenu->addAction(QString::fromUtf8(obs_module_text("Custom")));
	a->setCheckable(true);
	a->setChecked(!customBackupDir.empty());
	connect(a, &QAction::triggered, [this] {
		const QString dir = QFileDialog::getExistingDirectory(
			this, QString::fromUtf8(obs_module_text("BackupDir")),
			QString::fromUtf8(customBackupDir.c_str()),
			QFileDialog::ShowDirsOnly |
				QFileDialog::DontResolveSymlinks);
		if (dir.isEmpty())
			return;
		auto d = dir.toUtf8();
		customBackupDir = d.constData();
		const auto config = obs_frontend_get_global_config();
		config_set_string(config, "SceneCollectionManager", "BackupDir",
				  customBackupDir.c_str());
		on_sceneCollectionList_currentRowChanged(
			ui->sceneCollectionList->currentRow());
	});

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
			LoadBackupSceneCollection(
				item->text().toUtf8().constData(), filename,
				backupFile);
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
		os_globfree(glob);
	}
}

void SceneCollectionManagerDialog::on_sceneCollectionList_itemDoubleClicked(
	QListWidgetItem *item)
{
	UNUSED_PARAMETER(item);
	QMetaObject::invokeMethod(this,
				  "on_actionSwitchSceneCollection_triggered",
				  Qt::QueuedConnection);
}

void SceneCollectionManagerDialog::on_backupList_itemDoubleClicked(
	QListWidgetItem *item)
{
	UNUSED_PARAMETER(item);
	QMetaObject::invokeMethod(this, "on_actionSwitchBackup_triggered",
				  Qt::QueuedConnection);
}

void SceneCollectionManagerDialog::ReadSceneCollections()
{
	char *path = obs_module_config_path("../../basic/scenes/*.json");
	if (!path) {
		blog(LOG_WARNING, "Failed to get scene collections path");
		return;
	}
	char *path_abs = os_get_abs_path_ptr(path);
	os_glob_t *glob;
	if (os_glob(path, 0, &glob) != 0 && os_glob(path_abs, 0, &glob) != 0) {
		bfree(path);
		bfree(path_abs);
		blog(LOG_WARNING, "Failed to glob scene collections");
		return;
	}
	bfree(path);
	bfree(path_abs);
	scene_collections.clear();

	for (size_t i = 0; i < glob->gl_pathc; i++) {
		const char *filePath = glob->gl_pathv[i].path;

		if (glob->gl_pathv[i].directory)
			continue;

		auto *data =
			obs_data_create_from_json_file_safe(filePath, "bak");
		if (!data)
			continue;
		std::string name = obs_data_get_string(data, "name");
		obs_data_release(data);

		/* if no name found, use the file name as the name
		 * (this only happens when switching to the new version) */
		if (name.empty()) {
			auto p = strrchr(filePath, '/');
			if (!p)
				p = strrchr(filePath, '\\');
			if (p)
				name = p + 1;
			else
				name = filePath;
			if (name.size() > 5)
				name.resize(name.size() - 5);
		}
		scene_collections[QString::fromUtf8(name.c_str())] = filePath;
	}
	os_globfree(glob);
}

SceneCollectionManagerDialog::SceneCollectionManagerDialog(QMainWindow *parent)
	: QDialog(parent),
	  ui(new Ui::SceneCollectionManagerDialog)
{
	ui->setupUi(this);

	ReadSceneCollections();
	RefreshSceneCollections();
}

SceneCollectionManagerDialog::~SceneCollectionManagerDialog() {}
