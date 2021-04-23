#include "stdafx.h"
#include "UmaTextRecognizer.h"

#include <boost\algorithm\string\trim.hpp>
#include <boost\algorithm\string\replace.hpp>

#include <opencv2\opencv.hpp>

#include "Utility\CodeConvert.h"
#include "Utility\CommonUtility.h"
#include "Utility\json.hpp"
#include "Utility\timer.h"
#include "Utility\Logger.h"
#include "Utility\GdiplusUtil.h"

#include "TesseractWrapper.h"
using namespace TesseractWrapper;

using json = nlohmann::json;
using namespace CodeConvert;
using namespace cv;

void	LoadPointSizeFromJson(const json& json, const std::string& key, CPoint& pt, CSize& size)
{
	pt.x = json[key]["x"];
	pt.y = json[key]["y"];
	size.cx = json[key]["width"];
	size.cy = json[key]["height"];
}

void	SavePointSizeFromJson(json& json, const std::string& key, const CPoint& pt, const CSize& size)
{
	json[key]["x"] = pt.x;
	json[key]["y"] = pt.y;
	json[key]["width"] = size.cx;
	json[key]["height"] = size.cy;
}

cv::Mat GdiPlusBitmapToOpenCvMat(Gdiplus::Bitmap* bmp)
{
	auto format = bmp->GetPixelFormat();
	if (format != PixelFormat24bppRGB) {
		ERROR_LOG << L"GdiPlusBitmapToOpenCvMat: format != PixelFormat24bppRGB";
		ATLASSERT(FALSE);
		return cv::Mat();
	}

	int wd = bmp->GetWidth();
	int hgt = bmp->GetHeight();
	Gdiplus::Rect rcLock(0, 0, wd, hgt);
	Gdiplus::BitmapData bmpData;

	if (!bmp->LockBits(&rcLock, Gdiplus::ImageLockModeRead, format, &bmpData) == Gdiplus::Ok)
		return cv::Mat();

	cv::Mat mat = cv::Mat(hgt, wd, CV_8UC3, static_cast<unsigned char*>(bmpData.Scan0), bmpData.Stride).clone();

	bmp->UnlockBits(&bmpData);
	return mat;
}

cv::Rect	cvRectFromCRect(const CRect& rcBounds)
{
	return cv::Rect(rcBounds.left, rcBounds.top, rcBounds.Width(), rcBounds.Height());
}

bool	CheckCutBounds(const cv::Mat& img, const cv::Rect& rcCut, std::wstring comment)
{
	if (rcCut.x < 0 || rcCut.y < 0
		|| img.size().width < rcCut.x + rcCut.width || img.size().height < rcCut.y + rcCut.height) {
		ERROR_LOG << L"invalidate cute bounds: " << comment 
			<< L"src width: " << img.size().width << L" height: " << img.size().height 
			<< L" rcCut x: " << rcCut.x << L" y: " << rcCut.y << L" width: " << rcCut.width << L" height: " << rcCut.height;
		ATLASSERT(FALSE);
		return false;
	}
	return true;	
}


double ImageWhiteRatio(cv::Mat thresImage)
{
	int c = thresImage.channels();
	ATLASSERT(c == 1);

	// ��f���̔��̐��𐔂���
	int whilteCount = cv::countNonZero(thresImage);
	//int whilteCount = 0;
	//for (int y = 0; y < thresImage.rows; y++) {
	//	for (int x = 0; x < thresImage.cols; x++) {
	//		uchar val = thresImage.at<uchar>(y, x);
	//		if (val >= 255) {
	//			++whilteCount;
	//		}
	//	}
	//}
	const int imagePixelCount = thresImage.rows * thresImage.cols;
	const double whiteRatio = static_cast<double>(whilteCount) / imagePixelCount;
	return whiteRatio;
}


// ===============================================================================


// cutImage �ɂ���e�L�X�g���͂��͈͂𒲂ׂĕԂ�
// cutImage�͍��w�i�ɔ������ł���K�v������
// ���̂Ƃ���C�x���g���̎������g���Ă�
CRect GetTextBounds(cv::Mat cutImage, const CRect& rcBounds)
{
	//cv::Mat resizedImage;
	//constexpr double scale = 1.0;//4.0;
	//cv::resize(cutImage, resizedImage, cv::Size(), scale, scale, cv::INTER_CUBIC);
	//auto saveImagePath = GetExeDirectory() / L"debug1.png";
	//cv::imwrite(saveImagePath.string(), resizedImage);

	cv::Mat grayImage;
	cv::cvtColor(cutImage/*resizedImage*/, grayImage, cv::COLOR_RGB2GRAY);

	cv::Mat thresImage;
	cv::threshold(grayImage, thresImage, 0.0, 255.0, cv::THRESH_OTSU);

	int c = thresImage.channels();
	CPoint ptLT = { thresImage.cols , thresImage.rows };
	CPoint ptRB = { 0, 0 };
	for (int y = 0; y < thresImage.rows; y++) {
		for (int x = 0; x < thresImage.cols; x++) {
			uchar val = thresImage.at<uchar>(y, x);
			if (val >= 255) {
				// ���������������_�ň�ԏ������n�_��T��
				ptLT.y = std::min(ptLT.y, (LONG)y);
				ptLT.x = std::min(ptLT.x, (LONG)x);
				// ���������������_�ň�ԑ傫���n�_��T��
				ptRB.y = std::max(ptRB.y, (LONG)y);
				ptRB.x = std::max(ptRB.x, (LONG)x);
			}
		}
	}
	enum { 
		kTextMargin = 5 
	};
	CRect rcAdjustTextBounds(ptLT, ptRB);
	//rcAdjustTextBounds.top /= scale;
	//rcAdjustTextBounds.left /= scale;
	//rcAdjustTextBounds.right /= scale;
	//rcAdjustTextBounds.bottom /= scale;
	rcAdjustTextBounds.MoveToXY(rcBounds.left + rcAdjustTextBounds.left, rcBounds.top + rcAdjustTextBounds.top);

	rcAdjustTextBounds.InflateRect(kTextMargin, kTextMargin, kTextMargin, kTextMargin);	// �c��܂���
	ATLASSERT(rcAdjustTextBounds.Width() > 0 && rcAdjustTextBounds.Height() > 0);
	rcAdjustTextBounds.NormalizeRect();
	return rcAdjustTextBounds;
}

// ==========================================================================

bool UmaTextRecognizer::LoadSetting()
{
	HMODULE hModUser32 = ::LoadLibraryW(L"User32.dll");
	ATLASSERT(hModUser32);	
	m_funcSetThreadDpiAwarenessContext = (SetThreadDpiAwarenessContextFunc)::GetProcAddress(hModUser32, "SetThreadDpiAwarenessContext");

	std::ifstream ifs((GetExeDirectory() / L"UmaLibrary" / L"Common.json").wstring());
	ATLASSERT(ifs);
	if (!ifs) {
		ERROR_LOG << L"LoadSetting failed";
		return false;
	}
	json jsonCommon;
	ifs >> jsonCommon;

	m_targetWindowName = UTF16fromUTF8(jsonCommon["Common"]["TargetWindow"]["WindowName"].get<std::string>()).c_str();
	m_targetClassName = UTF16fromUTF8(jsonCommon["Common"]["TargetWindow"]["ClassName"].get<std::string>()).c_str();

	json jsonTestBounds = jsonCommon["Common"]["TestBounds"];

	m_baseClientSize.cx = jsonTestBounds["BaseClientSize"]["width"];
	m_baseClientSize.cy = jsonTestBounds["BaseClientSize"]["height"];

	for (int i = 0; i < kMaxCount; ++i) {
		CPoint pt;
		CSize size;
		LoadPointSizeFromJson(jsonTestBounds, UTF8fromUTF16(kTestBoundsName[i]), pt, size);
		CRect rcBounds = CRect(pt, size);
		m_testBounds[i] = rcBounds;
	}

	m_kCurrentTurnThreshold = jsonCommon["Common"]["ImageProcess"]["CurrentTurnThreshold"];

	for (const json& jsonTypo : jsonCommon["TypoDictionary"]) {
		std::wstring typo = UTF16fromUTF8(jsonTypo["Typo"].get<std::string>());
		std::wstring correct = UTF16fromUTF8(jsonTypo["Corret"].get<std::string>());
		m_typoDictionary[typo] = correct;
	}

	return true;
}

std::unique_ptr<Gdiplus::Bitmap> UmaTextRecognizer::ScreenShot()
{
	HWND hwndTarget = ::FindWindow(m_targetClassName, m_targetWindowName);
	if (!hwndTarget) {
		return nullptr;
	}

	CWindowDC dc(NULL/*hWndTarget*/);	// desktop

	if (m_funcSetThreadDpiAwarenessContext) {	// ��DPI���j�^�[�Ŏ擾�E�B���h�E�̈ʒu�������o�O��������邽��
		m_funcSetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	}

	CRect rcWindow;
	::GetWindowRect(hwndTarget, &rcWindow);

	CRect rcClient;
	::GetClientRect(hwndTarget, rcClient);

	if (m_funcSetThreadDpiAwarenessContext) {
		m_funcSetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED);
	}

	CRect rcAdjustClient = rcWindow;
	const int topMargin = (rcWindow.Height() - rcClient.Height() - GetSystemMetrics(SM_CXFRAME) * 2 - GetSystemMetrics(SM_CYCAPTION)) / 2;
	rcAdjustClient.top += GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CYCAPTION) + topMargin;
	rcAdjustClient.left += (rcWindow.Width() - rcClient.Width()) / 2;
	rcAdjustClient.right = rcAdjustClient.left + rcClient.right;
	rcAdjustClient.bottom = rcAdjustClient.top + rcClient.bottom;

	CDC dcMemory;
	dcMemory.CreateCompatibleDC(dc);

	LPVOID           lp;
	BITMAPINFO       bmi;
	BITMAPINFOHEADER bmiHeader = { sizeof(BITMAPINFOHEADER) };
	bmiHeader.biWidth = rcAdjustClient.Width();
	bmiHeader.biHeight = rcAdjustClient.Height();
	bmiHeader.biPlanes = 1;
	bmiHeader.biBitCount = 24;
	bmi.bmiHeader = bmiHeader;

	//CBitmap hbmp = ::CreateCompatibleBitmap(dc, rcAdjustClient.Width(), rcAdjustClient.Height());
	CBitmap hbmp = ::CreateDIBSection(dc, (LPBITMAPINFO)&bmi, DIB_RGB_COLORS, &lp, NULL, 0);
	auto prevhbmp = dcMemory.SelectBitmap(hbmp);

	//dcMemory.BitBlt(0, 0, rcWindow.Width(), rcWindow.Height(), dc, 0, 0, SRCCOPY);
	dcMemory.BitBlt(0, 0, rcAdjustClient.Width(), rcAdjustClient.Height(), dc, rcAdjustClient.left, rcAdjustClient.top, SRCCOPY);
	dcMemory.SelectBitmap(prevhbmp);
	return std::unique_ptr<Gdiplus::Bitmap>(Gdiplus::Bitmap::FromHBITMAP(hbmp, NULL));
}

bool UmaTextRecognizer::TextRecognizer(Gdiplus::Bitmap* image)
{
	m_umaMusumeName.clear();
	m_currentTurn.clear();
	m_eventName.clear();
	m_eventBottomOption.clear();

	std::unique_ptr<Gdiplus::Bitmap> bmpHolder;
	if (!image) {
		bmpHolder = ScreenShot();
		if (!bmpHolder) {
			return false;
		}
		image = bmpHolder.get();
	}

	cv::Mat srcImage = GdiPlusBitmapToOpenCvMat(image);
	if (srcImage.empty()) {
		ATLASSERT(FALSE);
		ERROR_LOG << L"GdiPlusBitmapToOpenCvMat failed";
		return false;
	}
	INFO_LOG << L"TextRecognizer start!";
	{	// �琬�E�}����
		INFO_LOG << L"�E�琬�E�}��";
		CRect rcSubName = _AdjustBounds(srcImage, m_testBounds[kUmaMusumeSubNameBounds]);
		CRect rcName = _AdjustBounds(srcImage, m_testBounds[kUmaMusumeNameBounds]);

		std::vector<std::wstring>	subNameList;
		std::vector<std::wstring>	nameList;

		auto funcImageToText = [this, &srcImage](int testBoundsIndex, std::vector<std::wstring>& list) {
			CRect rcName = _AdjustBounds(srcImage, m_testBounds[testBoundsIndex]);
			if (!CheckCutBounds(srcImage, cvRectFromCRect(rcName), L"rcName")) {
				return;
			}
			//Utility::timer timer;
			cv::Mat cutImage(srcImage, cvRectFromCRect(rcName));

			//cv::Mat resizedImage;
			//constexpr double scale = 4.0;
			//cv::resize(cutImage, resizedImage, cv::Size(), scale, scale, cv::INTER_CUBIC);

			cv::Mat grayImage;
			cv::cvtColor(cutImage/*resizedImage*/, grayImage, cv::COLOR_RGB2GRAY);

			cv::Mat thresImage;
			cv::threshold(grayImage, thresImage, 0.0, 255.0, cv::THRESH_OTSU);
			//INFO_LOG << L"Image processing1: " << timer.format();
			
			// �摜�ɂ����锒�w�i�����m�F���āA���䗦�ȉ��̂Ƃ��͖�������
			const double whiteRatio = ImageWhiteRatio(thresImage);
			constexpr double kMinWhiteRatioThreshold = 0.5;
			if (whiteRatio < kMinWhiteRatioThreshold) {
				return;
			}

			//INFO_LOG << L"Image processing2: " << timer.format() << L" whiteRatio: " << whiteRatio;

			std::wstring thresImageText = TextFromImage(thresImage);
			list.emplace_back(thresImageText);
		};

		funcImageToText(kUmaMusumeSubNameBounds, subNameList);
		if (subNameList.size()) {
			funcImageToText(kUmaMusumeNameBounds, nameList);
			if (nameList.size()) {
				const size_t size = subNameList.size();
				for (int i = 0; i < size; ++i) {
					std::wstring umamusumeName = subNameList[i] + nameList[i];
					m_umaMusumeName.emplace_back(umamusumeName);
				}
			}
		}

	}
	{	// �C�x���g��
		INFO_LOG << L"�E�C�x���g��";
		CRect rcEventName = _AdjustBounds(srcImage, m_testBounds[kEventNameBounds]);
		if (_IsEventNameIcon(srcImage)) {	// �A�C�R�������݂����ꍇ�A�F���͈͂��E�ɂ��炷
			enum { kIconTextMargin = 5 };
			rcEventName.left = _AdjustBounds(srcImage, m_testBounds[kEventNameIconBounds]).right + kIconTextMargin;
		}
		// �e�L�X�g���͂��͈͂�������
		if (!CheckCutBounds(srcImage, cvRectFromCRect(rcEventName), L"rcEventName")) {
			return false;
		}
		cv::Mat cutImage(srcImage, cvRectFromCRect(rcEventName));
		CRect rcAdjustTextBounds = GetTextBounds(cutImage, rcEventName);

		// �e�L�X�g�𐳊m�Ɉ͂����C���[�W��؂�o��
		if (!CheckCutBounds(srcImage, cvRectFromCRect(rcAdjustTextBounds), L"rcAdjustTextBounds")) {
			return false;
		}

		cv::Mat cutImage2(srcImage, cvRectFromCRect(rcAdjustTextBounds));

		cv::Mat grayImage;
		cv::cvtColor(cutImage2/*resizedImage*/, grayImage, cv::COLOR_RGB2GRAY);

		cv::Mat invertedImage;
		cv::bitwise_not(grayImage, invertedImage);

		cv::Mat resizedImage;
		constexpr double scale = 2.0;
		cv::resize(invertedImage, resizedImage, cv::Size(), scale, scale, cv::INTER_CUBIC);	

#if 0	// v1.2�܂ł̏���
		cv::Mat cutImage2(srcImage, cvRectFromCRect(rcAdjustTextBounds));
		//cv::imshow("cutImage2", cutImage2);
		// 
		//cv::Mat resizedImage;
		//constexpr double scale = 4.0;
		//cv::resize(cutImage, resizedImage, cv::Size(), scale, scale, cv::INTER_CUBIC);

		cv::Mat grayImage;
		cv::cvtColor(cutImage2/*resizedImage*/, grayImage, cv::COLOR_RGB2GRAY);

		cv::Mat invertedImage;
		cv::bitwise_not(grayImage, invertedImage);

		cv::Mat thresImage;
		cv::threshold(grayImage, thresImage, 0.0, 255.0, cv::THRESH_OTSU);
#endif
#if 0
		// 4�{
		cv::Mat resizedImage;
		constexpr double scale = 4.0;
		cv::resize(cutImage2, resizedImage, cv::Size(), scale, scale, cv::INTER_CUBIC);

		cv::Mat grayImage2;
		cv::cvtColor(resizedImage, grayImage2, cv::COLOR_RGB2GRAY);

		cv::Mat thresImage2;
		cv::threshold(grayImage2, thresImage2, 0.0, 255.0, cv::THRESH_OTSU);
#endif
		auto funcPushBackImageText = [this](cv::Mat& image, std::vector<std::wstring>& list) {
			std::wstring text = TextFromImage(image);

			// typo �𐳌�\�ŕϊ�
			auto itFound = m_typoDictionary.find(text);
			if (itFound != m_typoDictionary.end()) {
				text = itFound->second;
			}
			// '�I' �� '7' �Ɍ�F������Ă��܂����ۂ��̂Œu�����Č��ɒǉ����Ă���
			if (text.find(L"7") != std::wstring::npos) {
				std::wstring replacedText = boost::algorithm::replace_all_copy(text, L"7", L"�I");
				list.emplace_back(replacedText);
			}
			// '�I' �� '/' �Ɍ�F������Ă��܂����ۂ��̂Œu�����Č��ɒǉ����Ă���
			if (text.find(L"/") != std::wstring::npos) {
				std::wstring replacedText = boost::algorithm::replace_all_copy(text, L"/", L"�I");
				list.emplace_back(replacedText);
			}
			list.emplace_back(text);
		};

		funcPushBackImageText(resizedImage, m_eventName);	// 4 �O���[�X�P�[�����] + 2�{

		// �C�x���g�I����
		{
			CRect rcEventBottomOption = _AdjustBounds(srcImage, m_testBounds[kEventBottomOptionBounds]);
			cv::Mat cutImage(srcImage, cvRectFromCRect(rcEventBottomOption));

			cv::Mat grayImage;
			cv::cvtColor(cutImage, grayImage, cv::COLOR_RGB2GRAY);

			cv::Mat thresImage;
			cv::threshold(grayImage, thresImage, 0.0, 255.0, cv::THRESH_OTSU);

			cv::Mat resizedImage;
			constexpr double scale = 2.0;
			cv::resize(thresImage, resizedImage, cv::Size(), scale, scale, cv::INTER_CUBIC);

			funcPushBackImageText(resizedImage, m_eventBottomOption);	// 5 ���w�i������(�O���[臒l) + 2�{
		}
	}
	{	// ���݂̓��t
		INFO_LOG << L"�E���݂̓��t";
		CRect rcTurnBounds = _AdjustBounds(srcImage, m_testBounds[kCurrentTurnBounds]);
		if (!CheckCutBounds(srcImage, cvRectFromCRect(rcTurnBounds), L"rcTurnBounds")) {
			return false;
		}
		cv::Mat cutImage(srcImage, cvRectFromCRect(rcTurnBounds));

		//cv::Mat resizedImage;
		//constexpr double scale = 4.0;
		//cv::resize(cutImage, resizedImage, cv::Size(), scale, scale, cv::INTER_CUBIC);

		cv::Mat grayImage;
		cv::cvtColor(cutImage, grayImage, cv::COLOR_RGB2GRAY);

		cv::Mat thresImage;
		cv::threshold(grayImage, thresImage, 0.0, 255.0, cv::THRESH_OTSU);
		
		//cv::Mat manualThresholdImage;
		//cv::threshold(grayImage, manualThresholdImage, m_kCurrentTurnThreshold, 255.0, cv::THRESH_BINARY);

		//std::wstring manualThresImageText = TextFromImage(manualThresholdImage);
		//m_currentTurn.emplace_back(manualThresImageText);	// �D��

		std::wstring cutImageText = TextFromImage(cutImage);
		m_currentTurn.emplace_back(cutImageText);

		std::wstring thresImageText = TextFromImage(thresImage);
		m_currentTurn.emplace_back(thresImageText);

		//INFO_LOG << L"CurrentTurn, cut: " << cutImageText << L" thres: " << thresImageText;
	}
	{	// ���݃��j���[[�g���[�j���O]
		INFO_LOG << L"�E�g���[�j���O";
		m_bTrainingMenu = false;

		CRect rcCurrentMenuBounds = _AdjustBounds(srcImage, m_testBounds[kCurrentMenuBounds]);
		if (!CheckCutBounds(srcImage, cvRectFromCRect(rcCurrentMenuBounds), L"rcCurrentMenuBounds")) {
			return false;
		}
		cv::Mat cutImage(srcImage, cvRectFromCRect(rcCurrentMenuBounds));

		std::wstring cutImageText = TextFromImage(cutImage);
		if (cutImageText == L"�g���[�j���O") {
			CRect rcBackButtonBounds = _AdjustBounds(srcImage, m_testBounds[kBackButtonBounds]);
			if (!CheckCutBounds(srcImage, cvRectFromCRect(rcBackButtonBounds), L"rcBackButtonBounds")) {
				return false;
			}
			cv::Mat cutImage2(srcImage, cvRectFromCRect(rcBackButtonBounds));

			std::wstring cutImage2Text = TextFromImage(cutImage2);
			if (cutImage2Text == L"�߂�") {
				m_bTrainingMenu = true;
			}
		}
	}

	return true;
}

CRect UmaTextRecognizer::_AdjustBounds(const cv::Mat& srcImage, CRect bounds)
{
	//CSize imageSize(static_cast<int>(image->GetWidth()), static_cast<int>(image->GetHeight()));
	CSize imageSize(srcImage.size().width, srcImage.size().height);
	const double Xratio = static_cast<double>(imageSize.cx) / m_baseClientSize.cx;
	const double Yratio = static_cast<double>(imageSize.cy) / m_baseClientSize.cy;

	CRect adjustRect = bounds;
	adjustRect.top *= Yratio;
	adjustRect.left *= Xratio;
	adjustRect.right *= Xratio;
	adjustRect.bottom *= Yratio;

	return adjustRect;
}

bool UmaTextRecognizer::_IsEventNameIcon(cv::Mat srcImage)
{
	const CRect rcIcon = _AdjustBounds(srcImage, m_testBounds[kEventNameIconBounds]);

	cv::Mat cutImage(srcImage, cvRectFromCRect(rcIcon));

	ATLASSERT(cutImage.channels() == 3);	// must color image
	std::vector<cv::Mat> splitColors(3);	// �\�ߊm�ۂ��Ă��Ȃ��Ɨ�����
	cv::split(cutImage, splitColors);
	//cv::imshow("Blue", splitColors[0]);	// [0] -> Blue
	//cv::imshow("Green", splitColors[1]);// [1] -> Green
	//cv::imshow("Red", splitColors[2]);	// [2] -> Red

	cv::Mat blueThresImage;
	enum { kBlueThreshold = 230 };
	cv::threshold(splitColors[0], blueThresImage, kBlueThreshold, 255.0, cv::THRESH_BINARY);
	//cv::imshow("Blue thres", blueThresImage);
	const double blueRatio = ImageWhiteRatio(blueThresImage);
	constexpr double kBlueBackgroundThreshold = 0.9;	// �w�i����臒l
	if (kBlueBackgroundThreshold < blueRatio) {
		return false;	// �T�|�[�g�J�[�h�C�x���g
	}

	cv::Mat grayImage;
	cv::cvtColor(cutImage, grayImage, cv::COLOR_RGB2GRAY);

	cv::Mat thresImage;
	cv::threshold(grayImage, thresImage, kEventNameIconThreshold, 255.0, cv::THRESH_BINARY);

	const double whiteRatio = ImageWhiteRatio(thresImage);
	bool isIcon = whiteRatio > kEventNameIconWhiteRatioThreshold;	// ���̔䗦�����ȏ�Ȃ�A�C�R���Ƃ݂Ȃ�
	return isIcon;
}
