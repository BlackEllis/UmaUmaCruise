#include "stdafx.h"
#include "RaceDateLibrary.h"

#include <boost\algorithm\string\trim.hpp>
#include <boost\algorithm\string\replace.hpp>
#include <boost\filesystem.hpp>
#include <boost\optional.hpp>

#include "Utility\CodeConvert.h"
#include "Utility\CommonUtility.h"
#include "Utility\json.hpp"
#include "Utility\Logger.h"


using json = nlohmann::json;
using namespace CodeConvert;


boost::optional<std::wstring> retrieve(
	simstring::reader& dbr,
	const std::vector<std::wstring>& ambiguousEventNames,
	int measure,
	double threshold
);	// UmaEventLibrary.cpp

// =================================================

bool RaceDateLibrary::LoadRaceDataLibrary()
{
	_InitDB();

	ATLASSERT(m_allTurnList.size());
	m_turnOrderedRaceList.resize(m_allTurnList.size());

	std::ifstream ifs((GetExeDirectory() / L"UmaLibrary" / L"RaceDataLibrary.json").wstring());
	ATLASSERT(ifs);
	if (!ifs) {
		throw std::runtime_error("RaceDataLibrary.json �̓ǂݍ��݂Ɏ��s");
	}
	json jsonLibrary;
	ifs >> jsonLibrary;

	auto funcGradeFromText = [](const std::string& text) -> Race::Grade {
		if (text == "G1") {
			return Race::Grade::kG1;
		} else if (text == "G2") {
			return Race::Grade::kG2;
		} else if (text == "G3") {
			return Race::Grade::kG3;
		} else {
			ATLASSERT(FALSE);
			throw std::runtime_error("gradeText is not grade");
		}
	};
	auto funcGroundConditionFromText = [](const std::string& text) -> Race::GroundCondition {
		if (text == u8"��") {
			return Race::GroundCondition::kGrass;
		} else if (text == u8"�_�[�g") {
			return Race::GroundCondition::kDart;
		} else {
			ATLASSERT(FALSE);
			throw std::runtime_error("convert failed");
		}
	};
	auto funcDistanceClassFromText = [](const std::string& text) -> Race::DistanceClass {
		if (text == u8"�Z����") {
			return Race::DistanceClass::kSprint;
		} else if (text == u8"�}�C��") {
			return Race::DistanceClass::kMile;
		} else if (text == u8"������") {
			return Race::DistanceClass::kMiddle;
		} else if (text == u8"������") {
			return Race::DistanceClass::kLong;
		} else {
			ATLASSERT(FALSE);
			throw std::runtime_error("convert failed");
		}
	};
	auto funcRotationFromText = [](const std::string& text) -> Race::Rotation {
		if (text == u8"�E") {
			return Race::Rotation::kRight;
		} else if (text == u8"��") {
			return Race::Rotation::kLeft;
		} else if (text == u8"����") {
			return Race::Rotation::kLine;
		} else {
			ATLASSERT(FALSE);
			throw std::runtime_error("convert failed");
		}
	};
	auto funcLocationFlagFromText = [](const std::string& text) -> Race::Location {
		LPCSTR locationNames[Race::Location::kMaxLocationCount] = {
			u8"�D�y", u8"����", u8"����", u8"�V��", u8"����", u8"���R", u8"����", u8"���s", u8"��_", u8"���q", u8"���"
		};
		for (int i = 0; i < Race::Location::kMaxLocationCount; ++i) {
			LPCSTR location = locationNames[i];
			if (text == location) {
				Race::Location loc = static_cast<Race::Location>(Race::kSapporo << i);
				return loc;
			}
		}
		ATLASSERT(FALSE);
		throw std::runtime_error("convert failed");
	};

	for (const auto& items : jsonLibrary["Race"].items()) {
		std::string gradeText = items.key();
		const Race::Grade grade = funcGradeFromText(gradeText);
		for (const auto& jsonRace : items.value()) {
			auto raceData = std::make_shared<Race>();
			raceData->grade = grade;
			raceData->name = UTF16fromUTF8(jsonRace["Name"].get<std::string>());
			raceData->location = UTF16fromUTF8(jsonRace["Location"].get<std::string>());
			raceData->locationFlag = funcLocationFlagFromText(jsonRace["Location"].get<std::string>());
			raceData->groundCondition = funcGroundConditionFromText(jsonRace["GroundCondition"].get<std::string>());
			raceData->distanceClass = funcDistanceClassFromText(jsonRace["DistanceClass"].get<std::string>());
			raceData->distance = UTF16fromUTF8(jsonRace["Distance"].get<std::string>());
			raceData->rotation = funcRotationFromText(jsonRace["Rotation"].get<std::string>());
			for (const auto& jsonDate : jsonRace["Date"]) {
				std::wstring date = UTF16fromUTF8(jsonDate.get<std::string>());
				raceData->date.emplace_back(date);
				const int turn = GetTurnNumberFromTurnName(date);
				ATLASSERT(turn != -1);
				if (turn == -1) {
					throw std::runtime_error("_GetTurnNumberFromTurnName return -1");
				}
				m_turnOrderedRaceList[turn].push_back(raceData);
			}
		}
	}

	return true;
}

std::wstring RaceDateLibrary::AnbigiousChangeCurrentTurn(std::vector<std::wstring> ambiguousCurrentTurn)
{
	// Output similar strings from Unicode queries.
	auto optResult = retrieve(*m_dbReader, ambiguousCurrentTurn, simstring::exact, 1.0);
	if (!optResult) {
		optResult = retrieve(*m_dbReader, ambiguousCurrentTurn, simstring::cosine, 0.8);
	} else {
		m_searchCount = -1;	// exact
	}
	if (optResult) {
		std::wstring turn = optResult.get();
		if (m_currentTurn.length() && m_searchCount != -1) {	// ���t���t�߂肵�Ă��邩���ׂ�
			if (m_currentTurn != L"�t�@�C�i���Y�J�Ò�" && m_currentTurn != L"�W���j�A���f�r���[�O") {
				enum { kWrongTurnElapsedCount = 12 };
				const int turnNumber = GetTurnNumberFromTurnName(turn);
				const int prevTurnNumber = GetTurnNumberFromTurnName(m_currentTurn);
				if (std::abs(turnNumber - prevTurnNumber) >= kWrongTurnElapsedCount) {
					enum { kPassCount = 3 };
					++m_searchCount;
					if (m_searchCount < kPassCount) {
						// �O�񌟍����� kPassCount�ȓ��Ȃ�A�o�߃^�[�������ُ�Ȃ̂ŁA�O��̃^�[������Ԃ��悤�ɂ���
						WARN_LOG << L"AnbigiousChangeCurrentTurn, �o�߃^�[�������ُ�ł�"
							<< L" prevTurnNumber: " << prevTurnNumber << L" turnNumber: " << turnNumber;
						return m_currentTurn;
					} else {
						m_searchCount = 0;
						ATLASSERT(FALSE);	// �o�߃^�[���������������Ă��ʂ�
					}
				}
			}
		}
		m_searchCount = 0;
		m_currentTurn = turn;
		return m_currentTurn;
	} else {
		return L"";
	}
}

void RaceDateLibrary::_InitDB()
{
	auto dbFolder = GetExeDirectory() / L"simstringDB" / L"Turn";
	auto dbPath = dbFolder / L"turn_unicode.db";

	m_dbReader = std::make_unique<simstring::reader>();

	// DB�t�H���_���������ď�����
	if (boost::filesystem::is_directory(dbFolder)) {
		boost::system::error_code ec = {};
		boost::filesystem::remove_all(dbFolder, ec);
		if (ec) {
			ERROR_LOG << L"boost::filesystem::remove_all(dbFolder failed: " << (LPCWSTR)CA2W(ec.message().c_str());
		}
	}
	boost::filesystem::create_directories(dbFolder);

	// Open a SimString database for writing (with std::wstring).
	simstring::ngram_generator gen(3, false);	// bi-gram
	simstring::writer_base<std::wstring> dbw(gen, dbPath.string());

	// ���t�̈ꗗ���쐬����
	m_allTurnList.emplace_back(L"�W���j�A���f�r���[�O");
	dbw.insert(L"�W���j�A���f�r���[�O");
	LPCWSTR classList[3] = { L"�W���j�A��", L"�N���V�b�N��", L"�V�j�A��" };
	LPCWSTR halfMonth[2] = { L"�O��", L"�㔼" };
	for (std::wstring className : classList) {
		int i = 1;
		if (className == L"�W���j�A��") {
			i = 7;	// �W���j�A����7������
		}
		for (; i <= 12; ++i) {	// 1�� ~ 12��
			std::wstring classNameMonth = className + std::to_wstring(i) + L"��";
			for (LPCWSTR firstLast : halfMonth) {
				std::wstring date = classNameMonth + firstLast;
				m_allTurnList.emplace_back(date);
				dbw.insert(date);
			}
		}
	}
	m_allTurnList.emplace_back(L"�t�@�C�i���Y�J�Ò�");
	dbw.insert(L"�t�@�C�i���Y�J�Ò�");

	dbw.close();

	// Open the database for reading.
	m_dbReader->open(dbPath.string());
}

int RaceDateLibrary::GetTurnNumberFromTurnName(const std::wstring& searchTurn)
{
	const size_t size = m_allTurnList.size();
	for (size_t i = 0; i < size; ++i) {
		const std::wstring& turn = m_allTurnList[i];
		if (turn == searchTurn) {
			return i;
		}
	}
	return -1;
}


void Init()
{

}

std::wstring RaceDateLibrary::Race::RaceName() const
{
	switch (grade) {
	case kG1:
		return L"G1 " + name;
	case kG2:
		return L"G2 " + name;
	case kG3:
		return L"G3 " + name;
	default:
		ATLASSERT(FALSE);
	}
	return std::wstring();
}

std::wstring RaceDateLibrary::Race::GroundConditionText() const
{
	switch (groundCondition) {
	case kGrass:
		return L"��";
	case kDart:
		return L"�_�[�g";
	default:
		ATLASSERT(FALSE);
	}
	return std::wstring();
}

std::wstring RaceDateLibrary::Race::DistanceText() const
{
	switch (distanceClass) {
	case kSprint:
		return L"�Z�����i" + distance + L")";
	case kMile:
		return L"�}�C���i" + distance + L")";
	case kMiddle:
		return L"�������i" + distance + L")";
	case kLong:
		return L"�������i" + distance + L")";
	default:
		ATLASSERT(FALSE);
	}
	return std::wstring();
}

std::wstring RaceDateLibrary::Race::RotationText() const
{
	switch (rotation) {
	case kRight:
		return L"�E";
	case kLeft:
		return L"��";
	case kLine:
		return L"����";
	default:
		ATLASSERT(FALSE);
	}
	return std::wstring();
}

bool RaceDateLibrary::Race::IsMatchState(int32_t state)
{
	bool match = true;
	match &= (state & grade) != 0;
	match &= (state & groundCondition) != 0;
	match &= (state & distanceClass) != 0;
	match &= (state & rotation) != 0;
	match &= (state & locationFlag) != 0;
	return match;
}
