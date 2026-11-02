#include "stdafx.h"
#include "MissionRecordSet.h"

MissionRecordSet::MissionRecordSet(DaoDatabase* pDatabase) : DaoRecordset(pDatabase)
{
    m_MissionID = 0;
    m_MissionName = _T("");
    m_MissionFileName = _T("");
    m_MissionDateTime = static_cast<DATE>(0);
    m_TerrainFileName = _T("");
    m_ScriptFileName = _T("");
    m_BriefingFileName = _T("");
    m_ArtilleryAvailability = 0;
    m_SupplyPoints = 0;
    m_SensorTowers = 0;
    m_MapSize = 0;
    m_TimeLimit = 0;
    m_CaptureChance = 0;
    m_BriefingMapName = _T("");
    m_DefaultObjPoints = FALSE;
    m_ArtilleryLarge = 0;
    m_ArtillerySmall = 0;
    m_ArtilleryCamera = 0;
    m_ArtillerySensor = 0;
    m_PlayerForceLimit = 0;
    m_Multiplayer = FALSE;
    m_ClanArtilleryLarge = 0;
    m_ClanArtillerySmall = 0;
    m_ClanArtilleryCamera = 0;
    m_Planet = _T("");
    m_ClanArtillerySensor = 0;
    m_TimeOfDay = 0;
    m_Weather = 0;
    m_nFields = 28;
    m_nDefaultType = dbOpenDynaset;
}

CString MissionRecordSet::GetDefaultDBName()
{
    return _T("E:\\FIT\\teditor\\fitabl.mdb");
}

CString MissionRecordSet::GetDefaultSQL()
{
    return _T("[NewMissions]");
}

void MissionRecordSet::DoFieldExchange(DaoFieldExchange* pFX)
{
    pFX->SetFieldType(DaoFieldExchange::outputColumn);
    DFX_Long(pFX, _T("[MissionID]"), m_MissionID);
    DFX_Text(pFX, _T("[MissionName]"), m_MissionName, 255);
    DFX_Text(pFX, _T("[MissionFileName]"), m_MissionFileName, 255);
    DFX_DateTime(pFX, _T("[MissionDateTime]"), m_MissionDateTime);
    DFX_Text(pFX, _T("[TerrainFileName]"), m_TerrainFileName, 255);
    DFX_Text(pFX, _T("[ScriptFileName]"), m_ScriptFileName, 255);
    DFX_Text(pFX, _T("[BriefingFileName]"), m_BriefingFileName, 255);
    DFX_Long(pFX, _T("[ArtilleryAvailability]"), m_ArtilleryAvailability);
    DFX_Long(pFX, _T("[SupplyPoints]"), m_SupplyPoints);
    DFX_Short(pFX, _T("[SensorTowers]"), m_SensorTowers);
    DFX_Byte(pFX, _T("[MapSize]"), m_MapSize);
    DFX_Long(pFX, _T("[TimeLimit]"), m_TimeLimit);
    DFX_Short(pFX, _T("[CaptureChance]"), m_CaptureChance);
    DFX_Text(pFX, _T("[BriefingMapName]"), m_BriefingMapName, 255);
    DFX_Bool(pFX, _T("[DefaultObjPoints]"), m_DefaultObjPoints);
    DFX_Long(pFX, _T("[ArtilleryLarge]"), m_ArtilleryLarge);
    DFX_Long(pFX, _T("[ArtillerySmall]"), m_ArtillerySmall);
    DFX_Long(pFX, _T("[ArtilleryCamera]"), m_ArtilleryCamera);
    DFX_Long(pFX, _T("[ArtillerySensor]"), m_ArtillerySensor);
    DFX_Long(pFX, _T("[PlayerForceLimit]"), m_PlayerForceLimit);
    DFX_Bool(pFX, _T("[Multiplayer]"), m_Multiplayer);
    DFX_Long(pFX, _T("[ClanArtilleryLarge]"), m_ClanArtilleryLarge);
    DFX_Long(pFX, _T("[ClanArtillerySmall]"), m_ClanArtillerySmall);
    DFX_Long(pFX, _T("[ClanArtilleryCamera]"), m_ClanArtilleryCamera);
    DFX_Text(pFX, _T("[Planet]"), m_Planet, 255);
    DFX_Long(pFX, _T("[ClanArtillerySensor]"), m_ClanArtillerySensor);
    DFX_Long(pFX, _T("[TimeOfDay]"), m_TimeOfDay);
    DFX_Long(pFX, _T("[Weather]"), m_Weather);
}
