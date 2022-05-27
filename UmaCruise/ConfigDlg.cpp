#include "stdafx.h"
#include "ConfigDlg.h"

#include <wtl\atldlgs.h>

#include "Utility\json.hpp"
#include "Utility\CommonUtility.h"
#include "Utility\Logger.h"
#include "Utility\WinHTTPWrapper.h"

#include "WindowsGraphicsCaptureWrapper.h"

using json = nlohmann::json;
using namespace WinHTTPWrapper;

ConfigDlg::ConfigDlg(Config& config) : m_config(config)
{
}

LRESULT ConfigDlg::OnInitDialog(UINT, WPARAM, LPARAM, BOOL&)
{
	DoDataExchange(DDX_LOAD);

	enum { kMaxRefershCount = 10 };
	for (int i = 1; i <= kMaxRefershCount; ++i) {
		m_cmbRefreshInterval.AddString(std::to_wstring(i).c_str());
	}
	m_cmbRefreshInterval.SetCurSel(m_config.refreshInterval - 1);

	CComboBox cmbTheme = GetDlgItem(IDC_COMBO_THEME);
	LPCWSTR themeList[] = { L"自動", L"ダーク", L"ライト" };
	for (LPCWSTR themeText : themeList) {
		cmbTheme.AddString(themeText);
	}

	m_autoStart = m_config.autoStart;
	m_stopUpdatePreviewOnTraining = m_config.stopUpdatePreviewOnTraining;
	m_popupRaceListWindow = m_config.popupRaceListWindow;
	m_notifyFavoriteRaceHold = m_config.notifyFavoriteRaceHold;
	m_theme = static_cast<int>(m_config.theme);
	m_windowTopMost = m_config.windowTopMost;
	m_screenshotFolder = m_config.screenShotFolder.wstring().c_str();
	m_screenCaptureMethod = m_config.screenCaptureMethod;
	DoDataExchange(DDX_LOAD);

	if (!WindowsGraphicsCaptureWrapper::IsDllLoaded()) {
		GetDlgItem(IDC_RADIO_WINDOWSGRAPHICSCAPTURE).EnableWindow(FALSE);
	}

	DarkModeInit();

	return 0;
}

LRESULT ConfigDlg::OnOK(WORD, WORD wID, HWND, BOOL&)
{
	DoDataExchange(DDX_SAVE);

	if (m_screenshotFolder.GetLength()) {
		if (!fs::is_directory((LPCWSTR)m_screenshotFolder)) {
			MessageBox(L"スクリーンショット保存先のフォルダが存在しません", L"エラー", MB_ICONERROR);
			return 0;
		}
	}

	const int index = m_cmbRefreshInterval.GetCurSel();
	if (index == -1) {
		ATLASSERT(FALSE);
		ERROR_LOG << L"m_cmbRefreshInterval.GetCurSel == -1";
	} else {
		m_config.refreshInterval = index + 1;
	}
	m_config.autoStart = m_autoStart;
	m_config.stopUpdatePreviewOnTraining = m_stopUpdatePreviewOnTraining;
	m_config.popupRaceListWindow = m_popupRaceListWindow;
	m_config.notifyFavoriteRaceHold = m_notifyFavoriteRaceHold;
	m_config.theme = static_cast<Config::Theme>(m_theme);
	m_config.windowTopMost = m_windowTopMost;
	m_config.screenShotFolder = (LPCWSTR)m_screenshotFolder;
	m_config.screenCaptureMethod = static_cast<Config::ScreenCaptureMethod>(m_screenCaptureMethod);

	m_config.SaveConfig();

	EndDialog(IDOK);
	return 0;
}

LRESULT ConfigDlg::OnCancel(WORD, WORD, HWND, BOOL&)
{
	EndDialog(IDCANCEL);
	return 0;
}

void ConfigDlg::OnCheckUmaLibrary(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	try {
		std::ifstream ifs((GetExeDirectory() / L"UmaLibrary" / "Common.json").wstring());
		ATLASSERT(ifs);
		if (!ifs) {
			MessageBox(L"Common.json の読み込みに失敗");
			return;
		}
		json jsonCommon;
		ifs >> jsonCommon;
		std::string libraryURL = jsonCommon["Common"]["UmaMusumeLibraryURL"];
		libraryURL += "?" + std::to_string(std::time(nullptr));	// キャッシュ取得回避

		// ファイルサイズ取得
		auto umaLibraryPath = GetExeDirectory() / L"UmaLibrary" / L"UmaMusumeLibrary.json";
		const DWORD umaLibraryFileSize = static_cast<DWORD>(fs::file_size(umaLibraryPath));

		CUrl	downloadUrl(libraryURL.c_str());
		auto hConnect = HttpConnect(downloadUrl);
		auto hRequest = HttpOpenRequest(downloadUrl, hConnect, L"HEAD", L"", true);
		if (HttpSendRequestAndReceiveResponse(hRequest)) {
			int statusCode = HttpQueryStatusCode(hRequest);
			if (statusCode == 200) {
				DWORD contentLength = 0;
				HttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH, contentLength);
				if (umaLibraryFileSize != contentLength) {	// ファイルサイズ比較
					// 更新する
					auto optDLData = HttpDownloadData(downloadUrl.GetURL());
					if (optDLData) {
						// 古い方を残しておく
						auto prevPath = umaLibraryPath.parent_path() / (umaLibraryPath.stem().wstring() + L"_prev.json");
						fs::rename(umaLibraryPath, prevPath);

						SaveFile(umaLibraryPath, optDLData.get());
						MessageBox(L"更新しました！", L"成功");
						GetDlgItem(IDC_BUTTON_CHECK_UMALIBRARY).EnableWindow(FALSE);
						m_bUpdateLibrary = true;
						return;
					} else {
						MessageBox(L"ダウンロードに失敗しました...", L"エラー", MB_ICONERROR);
						return;
					}
				} else {
					MessageBox(L"更新は必要ありません", L"成功");
					GetDlgItem(IDC_BUTTON_CHECK_UMALIBRARY).EnableWindow(FALSE);
					return;
				}
			} else {
				CString errorText;
				errorText.Format(L"サーバーからエラーが返されました。\nステータスコード: %d", statusCode);
				MessageBox(errorText, L"エラー", MB_ICONERROR);
				return;
			}
		} else {
			MessageBox(L"リクエストの送信に失敗しました。\n詳細は info.log を参照してください。", L"エラー", MB_ICONERROR);
			return;
		}
	} catch (boost::exception& e) {
		std::string expText = boost::diagnostic_information(e);
		ERROR_LOG << L"OnCheckUmaLibrary exception: " << (LPCWSTR)CA2W(expText.c_str());
		int a = 0;
	}
	ATLASSERT(FALSE);
	MessageBox(L"何かしらのエラーが発生しました...", L"エラー", MB_ICONERROR);
}

// スクリーンショットの保存先フォルダを選択する
void ConfigDlg::OnScreenShotFolderSelect(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	std::thread([this]() {
		DWORD dwOptions = FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_FILEMUSTEXIST;
		CShellFileOpenDialog dlg(nullptr, dwOptions);
		auto ret = dlg.DoModal(m_hWnd);
		if (ret == IDOK) {
			dlg.GetFilePath(m_screenshotFolder);
			DoDataExchange(DDX_LOAD, IDC_EDIT_SS_FOLDER);
		}
	}).detach();

}
