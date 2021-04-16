
#pragma once

#include <memory>
#include <winhttp.h>
#include <boost/exception/all.hpp>
#include <boost/optional.hpp>

namespace WinHTTPWrapper {

bool	InitWinHTTP(boost::optional<CString> optUserAgent = boost::none, boost::optional<CString> optProxy = boost::none);
void	TermWinHTTP();

struct HINTERNET_deleter
{
	typedef HINTERNET	pointer;
	void operator () (HINTERNET handle) {
		ATLVERIFY(::WinHttpCloseHandle(handle));
	}
};

typedef std::unique_ptr<HINTERNET, HINTERNET_deleter>	INetHandle;


typedef boost::error_info<struct err_info, DWORD> ex_add_info;

struct WinHTTPException : virtual boost::exception, virtual std::exception
{
	WinHTTPException() 
	{
		dwGetLastError = ::GetLastError();
		*this << ex_add_info(dwGetLastError);
	}

	DWORD	dwGetLastError;
};

class CUrl
{
public:
	CUrl() { }

	CUrl(const CString& url) : m_url(url)
	{
		Set(url);
	}

	CUrl(const std::wstring& url) : m_url(url.c_str())
	{
		Set(url.c_str());
	}

	void	Set(const CString& url)
	{
		URL_COMPONENTS urlComp = { sizeof(URL_COMPONENTS) };
		urlComp.dwHostNameLength	= -1;
		urlComp.dwUrlPathLength		= -1;
		if (::WinHttpCrackUrl(url, url.GetLength(), 0, &urlComp) == FALSE) 
			BOOST_THROW_EXCEPTION(WinHTTPException());
		m_hostName	= CString(urlComp.lpszHostName, urlComp.dwHostNameLength);
		m_path		= CString(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
		if (urlComp.nScheme == INTERNET_SCHEME_HTTPS)
			m_optSSLPort = urlComp.nPort;
	}

	const CString&	GetURL() const { return m_url; }
	const CString&	GetHost() const { return m_hostName; }
	const CString&	GetPath() const { return m_path; }
	boost::optional<int>	GetSSLPortNumber() const { return m_optSSLPort; }

private:
	CString	m_url;
	CString	m_hostName;
	CString	m_path;
	boost::optional<int>	m_optSSLPort;
};

/// url�Ŏw�肵���T�[�o�[�Ƃ̃R�l�N�V�������쐬����
INetHandle	HttpConnect(const CUrl& url);

/// �R�l�N�V����������ۂ̃��\�[�X�ɑ΂��郊�N�G�X�g���쐬����
INetHandle	HttpOpenRequest(const CUrl& url, const INetHandle& hConnect, LPCWSTR Verb = L"GET", const CString& referer = CString());

/// ���N�G�X�g�w�b�_��ǉ�����
void		HttpAddRequestHeaders(const INetHandle& hRequest, const CString& addHeaders, DWORD dwModifiers = WINHTTP_ADDREQ_FLAG_ADD);

/// ���N�G�X�g�̐ݒ��ύX����
void		HttpSetOption(const INetHandle& hRequest, DWORD option, DWORD optionValue);

/// ���N�G�X�g�Ƀv���N�V��ݒ肷��
void		HttpSetProxy(const INetHandle& hRequest, const CString& proxy);

/// ���N�G�X�g�𑗐M������A����������܂ő҂�
bool		HttpSendRequestAndReceiveResponse(const INetHandle& hRequest, const std::string& postData = std::string());

/// ���N�G�X�g���ʂ̃X�e�[�^�X�R�[�h��Ԃ�
DWORD		HttpQueryStatusCode(const INetHandle& hRequest);

/// InfoLevel�Ŏw�肵�����X�|���X�w�b�_��Ԃ�
bool		HttpQueryHeaders(const INetHandle& hRequest, DWORD InfoLevel, CString& headerContents);

/// InfoLevel�Ŏw�肵�����X�|���X�w�b�_��Ԃ�
bool		HttpQueryHeaders(const INetHandle& hRequest, DWORD InfoLevel, DWORD& headerContents);

// ���X�|���X�w�b�_�𐶂̂܂ܕԂ�
bool		HttpQueryRawHeaders(const INetHandle& hRequest, CString& rawHeaderContents);

/// ���X�|���X����{�f�B�������擾����
std::string HttpReadData(const INetHandle& hRequest);

/// url����_�E�����[�h����
boost::optional<std::string>	HttpDownloadData(const CString& url);


}	// namespace WinHTTPWrapper