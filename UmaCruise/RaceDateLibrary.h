#pragma once

#include <string>
#include <memory>
#include <vector>
#include <chrono>

#include "simstring\simstring.h"

class RaceDateLibrary
{
public:
	struct Race {
		enum Grade {
			kG1 = 1 << 0,
			kG2 = 1 << 1,
			kG3 = 1 << 2,
		};
		Grade			grade;				// G1�EG2�EG3
		std::wstring	name;				// ���[�X��
		std::wstring	location;			// �ꏊ
		enum GroundCondition {
			kGrass = 1 << 3,
			kDart  = 1 << 4,
		};
		GroundCondition	groundCondition;	// �ŁE�_�[�g
		enum DistanceClass {
			kSprint = 1 << 5,
			kMile	= 1 << 6,
			kMiddle	= 1 << 7,
			kLong	= 1 << 8,
		};
		DistanceClass	distanceClass;		// �Z�����E�}�C���E�������E������
		std::wstring	distance;			// ��̎��ۂ̋�����
		enum Rotation {
			kRight	= 1 << 9, 
			kLeft	= 1 << 10,
			kLine	= 1 << 11,
		};
		Rotation		rotation;			// �E�E�����E����
		std::vector<std::wstring>	date;	// �J�Ó�

		// =============================
		std::wstring	RaceName() const;
		std::wstring	GroundConditionText() const;
		std::wstring	DistanceText() const;
		std::wstring	RotationText() const;

		// ����ނ�OR�����A�ʎ�ނ�AND����
		bool	IsMatchState(int32_t state);
	};

	// RaceDataLibrary.json ��ǂݍ���
	bool	LoadRaceDataLibrary();

	// �S�^�[�����X�g
	const std::vector<std::wstring>& GetAllTurnList() const {
		return m_allTurnList;
	}
	// �^�[�����̑S���[�X���X�g
	const std::vector<std::vector<std::shared_ptr<Race>>>& GetTurnOrderedRaceList() const {
		return m_turnOrderedRaceList;
	}

	// 
	int		GetTurnNumberFromTurnName(const std::wstring& searchTurn);


	// �����܂������Ō��݂̓��t��ύX����
	std::wstring	AnbigiousChangeCurrentTurn(std::vector<std::wstring> ambiguousCurrentTurn);

private:
	void	_InitDB();

	std::wstring	m_currentTurn;
	int		m_searchCount = 0;	// �t�s�E�k�s�h�~�̂���

	std::vector<std::wstring> m_allTurnList;

	std::unique_ptr<simstring::reader>	m_dbReader;

	std::vector<std::vector<std::shared_ptr<Race>>>	m_turnOrderedRaceList;
};

