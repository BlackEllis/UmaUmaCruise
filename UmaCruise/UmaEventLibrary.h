#pragma once

#include <string>
#include <array>
#include <vector>
#include <memory>
#include <mutex>
#include <boost\optional.hpp>

#include "simstring\simstring.h"


class UmaEventLibrary
{
public:
	struct EventOptionEffect {
		std::wstring	option;	// �I����
		std::wstring	effect;	// ����
	};
	enum { kMaxOption = 3 };	// 1�̃C�x���g�ɕt���A�ő�3�̑I����������
	typedef std::array<EventOptionEffect, kMaxOption>	EventOptions;

	struct UmaEvent {
		std::wstring	eventName;		// �C�x���g��
		EventOptions	eventOptions;
	};

	// ��l�̃L���������L����S�C�x���g����������
	struct CharaEvent {
		std::wstring	name;	// �L������ / �T�|�[�g��
		std::wstring	property;	// [��1, ��2, ��3] / [SSR, SR, R]
		std::vector<UmaEvent>	umaEventList;	// �L���������L����S�C�x���g���X�g
	};
	typedef std::vector<std::unique_ptr<CharaEvent>>	CharaEventList;

	// UmaMusumeLibrary.json ��ǂݍ���
	bool	LoadUmaMusumeLibrary();

	// ���ݑI�𒆂̈琬�E�}������Ԃ�
	const std::wstring& GetCurrentIkuseiUmaMusume() const {
		return m_currentIkuseUmaMusume;
	}

	// �琬�E�}���̃C�x���g���X�g��Ԃ�
	const CharaEventList& GetIkuseiUmaMusumeEventList() const {
		return m_charaEventList;
	}

	// �����ΏۂƂ���琬�E�}����ύX����
	void	ChangeIkuseiUmaMusume(const std::wstring& umaName);

	// �����܂������ň琬�E�}����ύX����
	void	AnbigiousChangeIkuseImaMusume(std::vector<std::wstring> ambiguousUmaMusumeNames);

	// �����܂������ŃC�x���g����T��
	boost::optional<UmaEvent>	AmbiguousSearchEvent(
		const std::vector<std::wstring>& ambiguousEventNames,
		const std::vector<std::wstring>& ambiguousEventBottomOptions );

	// �C�x���g���ǂ̃L�������甭�������̂���Ԃ�
	const std::wstring& GetLastEventSource() const {
		return m_lastEventSource;
	}

private:
	void		_DBUmaNameInit();
	void		_DBInit();
	UmaEvent	_SearchEventOptions(const std::wstring& eventName);
	UmaEvent	_SearchEventOptionsFromBottomOption(const std::wstring& bottomOption);

	// ���ݑI�𒆂̈琬�E�}���̖��O
	std::wstring	m_currentIkuseUmaMusume;
	std::mutex		m_mtxName;

	// �琬�E�}��
	CharaEventList	m_charaEventList;
	// �T�|�[�g�J�[�h
	CharaEventList	m_supportEventList;

	std::wstring	m_lastEventSource;

	bool	m_simstringDBInit = false;
	std::unique_ptr<simstring::reader>	m_dbUmaNameReader;
	std::unique_ptr<simstring::reader>	m_dbReader;
	std::unique_ptr<simstring::reader>	m_dbOptionReader;

	double	m_kMinThreshold = 0.4;
};

