#pragma once

#include "DaoRecordset.h"

/// <summary>The rows of a mission (NewMissions), every column bound.</summary>
/// <remarks>Constructor 0x460940; 0x104 bytes in the original.</remarks>
class MissionRecordSet : public DaoRecordset
{
public:
    /// <summary>A closed recordset on <paramref name="pDatabase"/>, its fields zero or empty.</summary>
    explicit MissionRecordSet(DaoDatabase* pDatabase = nullptr);

    /// <summary>The MissionID column.</summary>
    long m_MissionID;
    /// <summary>The MissionName column.</summary>
    CString m_MissionName;
    /// <summary>The MissionFileName column.</summary>
    CString m_MissionFileName;
    /// <summary>The MissionDateTime column.</summary>
    COleDateTime m_MissionDateTime;
    /// <summary>The TerrainFileName column.</summary>
    CString m_TerrainFileName;
    /// <summary>The ScriptFileName column.</summary>
    CString m_ScriptFileName;
    /// <summary>The BriefingFileName column.</summary>
    CString m_BriefingFileName;
    /// <summary>The ArtilleryAvailability column.</summary>
    long m_ArtilleryAvailability;
    /// <summary>The SupplyPoints column.</summary>
    long m_SupplyPoints;
    /// <summary>The SensorTowers column.</summary>
    short m_SensorTowers;
    /// <summary>The MapSize column.</summary>
    BYTE m_MapSize;
    /// <summary>The TimeLimit column.</summary>
    long m_TimeLimit;
    /// <summary>The CaptureChance column.</summary>
    short m_CaptureChance;
    /// <summary>The BriefingMapName column.</summary>
    CString m_BriefingMapName;
    /// <summary>The DefaultObjPoints column.</summary>
    BOOL m_DefaultObjPoints;
    /// <summary>The ArtilleryLarge column.</summary>
    long m_ArtilleryLarge;
    /// <summary>The ArtillerySmall column.</summary>
    long m_ArtillerySmall;
    /// <summary>The ArtilleryCamera column.</summary>
    long m_ArtilleryCamera;
    /// <summary>The ArtillerySensor column.</summary>
    long m_ArtillerySensor;
    /// <summary>The PlayerForceLimit column.</summary>
    long m_PlayerForceLimit;
    /// <summary>The Multiplayer column.</summary>
    BOOL m_Multiplayer;
    /// <summary>The ClanArtilleryLarge column.</summary>
    long m_ClanArtilleryLarge;
    /// <summary>The ClanArtillerySmall column.</summary>
    long m_ClanArtillerySmall;
    /// <summary>The ClanArtilleryCamera column.</summary>
    long m_ClanArtilleryCamera;
    /// <summary>The Planet column.</summary>
    CString m_Planet;
    /// <summary>The ClanArtillerySensor column.</summary>
    long m_ClanArtillerySensor;
    /// <summary>Port-only: the TimeOfDay column (MissionTimeOfDay.h).</summary>
    long m_TimeOfDay;

    /// <summary>The class wizard's database path (unused: the editor gives every recordset its database).</summary>
    CString GetDefaultDBName() override;
    /// <summary>The table, [NewMissions].</summary>
    CString GetDefaultSQL() override;
    /// <summary>Binds every field to its column.</summary>
    /// <remarks>0x460b70.</remarks>
    void DoFieldExchange(DaoFieldExchange* pFX) override;
};
