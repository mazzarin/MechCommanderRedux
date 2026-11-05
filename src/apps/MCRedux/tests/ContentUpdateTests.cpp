#include "stdafx.h"
#include "MCContentFixture.h"
#include "MCScratchFolder.h"
#include "MCTest.h"
#include "lib/MCSha256.h"
#include "platform/MCFileSystem.h"
#include "update/MCContentFolder.h"
#include "update/MCContentList.h"
#include "update/MCContentPlan.h"
#include "update/MCInstalled.h"
#include "update/MCReleaseFile.h"
#include "update/MCSignedFile.h"
#include "update/MCUpdateJson.h"

#include <regex>

namespace
{
    using MCContentFixture::KeyList;
    using MCContentFixture::KeyPair;
    using MCContentPlan::MCAction;
    using MCContentSwap::MCKeep;

    /// <summary>A scratch folder of the test's own, deleted with it.</summary>
    class ScratchFolder : public MCScratchFolder
    {
    public:
        explicit ScratchFolder(std::string_view name) : MCScratchFolder("mc_content_update_tests", name) {}
    };

    MCReleaseFile::MCRelease Release(uint64_t number)
    {
        MCReleaseFile::MCRelease release;
        release.Number = number;
        release.Version = std::format("0.2.{}", number);
        release.Notes = "Fixes.";
        release.List = MCSha256::Of(std::format("list {}", number));
        release.ListSize = 1000 + number;
        release.Programs["mac"] = MCReleaseFile::MCProgram{.Build = 500 + number, .Size = 7'000'000};
        release.Programs["windows"] = MCReleaseFile::MCProgram{.Build = 500 + number,
                                                               .Size = 12'000'000,
                                                               .Sha256 = MCSha256::Of("setup"),
                                                               .Url = "https://example.invalid/setup.exe",
                                                               .Silent = "/VERYSILENT"};
        return release;
    }

    /// <summary><c>release.json</c> at <paramref name="issue"/>, current release <paramref name="current"/>, signed.</summary>
    std::string Releases(const KeyPair& key, uint64_t issue, uint64_t current,
                         std::optional<std::pair<uint64_t, double>> rollout = std::nullopt)
    {
        MCReleaseFile::MCReleases releases{.Format = 1, .Issue = issue, .Current = Release(current)};

        if (rollout)
        {
            releases.Rollout = Release(rollout->first);
            releases.RolloutShare = rollout->second;
        }

        return key.Envelope(MCSignedFile::ReleaseType, MCReleaseFile::ReleasesPayload(releases));
    }

    std::string Base64Of(std::string_view text)
    {
        return MCUpdateJson::ToBase64(std::span(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
    }

    /// <summary>Writes <paramref name="text"/> as the whole of a file, its folders made (the content fixture's).</summary>
    constexpr auto& WriteText = MCContentFixture::Write;

    /// <summary>A listed file with <paramref name="text"/> as its bytes.</summary>
    MCContentList::MCFile Listed(std::string path, std::string_view text, std::string zip = "base-1.zip",
                                 std::string set = std::string(MCContentList::GameSet))
    {
        return MCContentList::MCFile{.Path = std::move(path),
                                     .Set = std::move(set),
                                     .Size = text.size(),
                                     .Sha256 = MCSha256::Of(text),
                                     .Zip = std::move(zip)};
    }

    const MCContentPlan::MCStep* StepFor(const MCContentPlan::MCPlan& plan, std::string_view path)
    {
        const auto found = std::ranges::find(plan.Steps, path, &MCContentPlan::MCStep::Path);
        return found == plan.Steps.end() ? nullptr : &*found;
    }

    std::optional<MCAction> ActionFor(const MCContentPlan::MCPlan& plan, std::string_view path)
    {
        const MCContentPlan::MCStep* const step = StepFor(plan, path);
        return step != nullptr ? std::optional(step->Action) : std::nullopt;
    }

    const std::set<std::string, std::less<>> GameOnly = {std::string(MCContentList::GameSet)};
    const std::set<std::string, std::less<>> GameAndHd = {std::string(MCContentList::GameSet),
                                                          std::string(MCContentList::HdSet)};
}

TEST_CASE("sha256: the standard's test vector, a file read in parts, and hex both ways")
{
    CHECK_EQ(MCSha256::Hex(MCSha256::Of("abc")),
             std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    const ScratchFolder scratch("sha256");
    std::string big(3 << 20, '\0');

    for (size_t i = 0; i < big.size(); ++i)
    {
        big[i] = static_cast<char>((i * 2654435761u) >> 24);
    }

    WriteText(scratch.Path() / "big.bin", big);
    const auto file = MCSha256::OfFile(scratch.Path() / "big.bin");
    REQUIRE(file.has_value());
    CHECK(*file == MCSha256::Of(big));
    CHECK(MCSha256::FromHex(MCSha256::Hex(*file)) == *file);
    CHECK(MCSha256::FromHex("BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD") == MCSha256::Of("abc"));
    CHECK(!MCSha256::FromHex("ba7816bf").has_value());
    CHECK(!MCSha256::FromHex(std::string(64, 'g')).has_value());
    CHECK(!MCSha256::OfFile(scratch.Path() / "missing.bin").has_value());
}

TEST_CASE("update files: a signed file opens with its key and type, never another key, type or a changed byte")
{
    const KeyPair key(1);
    const KeyPair other(2);
    const std::string payload = R"({"hello": "world"})";
    const std::string envelope = key.Envelope(MCSignedFile::ReleaseType, payload);
    const std::array<MCSignedFile::MCPublicKey, 1> keys = {key.Public};
    const std::array<MCSignedFile::MCPublicKey, 1> otherKeys = {other.Public};
    const std::array bothKeys = {other.Public, key.Public};

    const auto opened = MCSignedFile::Open(envelope, MCSignedFile::ReleaseType, keys);
    REQUIRE(opened.has_value());
    CHECK_EQ(*opened, payload);
    CHECK(MCSignedFile::Open(envelope, MCSignedFile::ReleaseType, bothKeys).has_value());
    CHECK(!MCSignedFile::Open(envelope, MCSignedFile::ReleaseType, otherKeys).has_value());
    // A key list can't be passed off as a release file, nor the other way: the type is signed with the payload.
    CHECK(!MCSignedFile::Open(envelope, MCSignedFile::KeyListType, keys).has_value());
    const std::string relabelled = std::regex_replace(envelope, std::regex("release\\+json"), "keys+json");
    CHECK(!MCSignedFile::Open(relabelled, MCSignedFile::KeyListType, keys).has_value());

    // Another payload under the same signature fails.
    const std::string swapped = std::regex_replace(envelope, std::regex("\"payload\": \"[^\"]+\""),
                                                   "\"payload\": \"" + Base64Of(R"({"hello": "there"})") + "\"");
    CHECK(swapped != envelope);
    CHECK(!MCSignedFile::Open(swapped, MCSignedFile::ReleaseType, keys).has_value());

    // Not JSON, no signatures, a signature of the wrong length: refused, with a reason.
    const auto notJson = MCSignedFile::Open("hello", MCSignedFile::ReleaseType, keys);
    REQUIRE(!notJson.has_value());
    CHECK(notJson.error().find("isn't JSON") != std::string::npos);
    const std::array<std::vector<uint8_t>, 1> shortSignature = {std::vector<uint8_t>(10, 1)};
    CHECK(!MCSignedFile::Open(MCSignedFile::Envelope(MCSignedFile::ReleaseType, payload, shortSignature),
                              MCSignedFile::ReleaseType, keys)
               .has_value());
    CHECK(!MCSignedFile::Open(MCSignedFile::Envelope(MCSignedFile::ReleaseType, payload, {}), MCSignedFile::ReleaseType,
                              keys)
               .has_value());
}

TEST_CASE("update files: release.json is taken only from a release key the root's list names, never older")
{
    const KeyPair root(10);
    const KeyPair first(11);
    const KeyPair second(12);
    const std::array<MCSignedFile::MCPublicKey, 1> roots = {root.Public};

    const auto taken = MCReleaseFile::Check(KeyList(root, 2, {first.Public}), Releases(first, 5, 7), roots, {});
    REQUIRE(taken.has_value());
    CHECK_EQ(taken->Releases.Current.Number, uint64_t{7});
    CHECK_EQ(taken->Releases.Current.Version, std::string("0.2.7"));
    CHECK_EQ(taken->Releases.Current.Programs.at("mac").Build, uint64_t{507});
    CHECK(taken->Releases.Current.Programs.at("windows").Sha256 == MCSha256::Of("setup"));
    CHECK_EQ(taken->Releases.Current.Programs.at("windows").Silent, std::string("/VERYSILENT"));
    CHECK(!taken->Releases.Current.Programs.at("mac").Sha256.has_value());

    // A release key the list doesn't name, or a list the root didn't sign.
    CHECK(!MCReleaseFile::Check(KeyList(root, 2, {first.Public}), Releases(second, 5, 7), roots, {}).has_value());
    CHECK(!MCReleaseFile::Check(KeyList(second, 2, {second.Public}), Releases(second, 5, 7), roots, {}).has_value());

    // Never older than what was read: the same issue again is taken.
    const MCReleaseFile::MCKept kept = MCReleaseFile::Keep({}, *taken);
    CHECK_EQ(kept.KeyListIssue, uint64_t{2});
    CHECK_EQ(kept.ReleaseIssue, uint64_t{5});
    CHECK(MCReleaseFile::Check(KeyList(root, 2, {first.Public}), Releases(first, 5, 7), roots, kept).has_value());
    const auto olderRelease =
        MCReleaseFile::Check(KeyList(root, 2, {first.Public}), Releases(first, 4, 8), roots, kept);
    REQUIRE(!olderRelease.has_value());
    CHECK(olderRelease.error().find("older") != std::string::npos);
    CHECK(!MCReleaseFile::Check(KeyList(root, 1, {first.Public}), Releases(first, 6, 8), roots, kept).has_value());

    // The release key replaced: the new list names the second key, and the first's files are refused from then.
    const std::string replaced = KeyList(root, 3, {second.Public});
    CHECK(MCReleaseFile::Check(replaced, Releases(second, 6, 8), roots, kept).has_value());
    CHECK(!MCReleaseFile::Check(replaced, Releases(first, 6, 8), roots, kept).has_value());

    // The list's lowest release overrides a release.json numbered below it.
    const auto belowLowest =
        MCReleaseFile::Check(KeyList(root, 4, {first.Public}, 10), Releases(first, 7, 9), roots, kept);
    REQUIRE(!belowLowest.has_value());
    CHECK(belowLowest.error().find("lowest") != std::string::npos);
}

TEST_CASE("update files: a rollout reaches its share of copies and no others; widening brings more")
{
    MCReleaseFile::MCReleases releases{.Format = 1, .Issue = 1, .Current = Release(7)};
    releases.Rollout = Release(8);
    constexpr uint32_t Copies = 10'000;
    const auto taking = [&](double share)
    {
        releases.RolloutShare = share;
        std::vector<bool> took;

        for (uint32_t i = 0; i < Copies; ++i)
        {
            const auto number = static_cast<uint32_t>(uint64_t{i} * (uint64_t{1} << 32) / Copies);
            took.push_back(MCReleaseFile::For(releases, number).Number == 8);
        }

        return took;
    };

    const std::vector<bool> none = taking(0.0);
    const std::vector<bool> quarter = taking(25.0);
    const std::vector<bool> half = taking(50.0);
    const std::vector<bool> all = taking(100.0);
    CHECK_EQ(std::ranges::count(none, true), 0);
    CHECK_EQ(std::ranges::count(quarter, true), Copies / 4);
    CHECK_EQ(std::ranges::count(half, true), Copies / 2);
    CHECK_EQ(std::ranges::count(all, true), Copies);

    for (uint32_t i = 0; i < Copies; ++i)
    {
        CHECK(!quarter[i] || half[i]);
    }

    releases.RolloutShare = 100.0;
    CHECK_EQ(MCReleaseFile::For(releases, UINT32_MAX).Number, uint64_t{8});
    releases.Rollout.reset();
    CHECK_EQ(MCReleaseFile::For(releases, 0).Number, uint64_t{7});

    // A rollout that isn't newer, or a share past 100, is refused.
    MCReleaseFile::MCReleases notNewer{
        .Format = 1, .Issue = 1, .Current = Release(7), .Rollout = Release(7), .RolloutShare = 10.0};
    CHECK(!MCReleaseFile::ReadReleases(MCReleaseFile::ReleasesPayload(notNewer)).has_value());
    MCReleaseFile::MCReleases tooMuch{
        .Format = 1, .Issue = 1, .Current = Release(7), .Rollout = Release(8), .RolloutShare = 150.0};
    CHECK(!MCReleaseFile::ReadReleases(MCReleaseFile::ReleasesPayload(tooMuch)).has_value());
}

TEST_CASE("content list: paths outside the folder, alike but for case, or that Windows can't hold are refused")
{
    for (const std::string_view good :
         {"data/missions/m01.fit", "HD/terrain.hda", "ART.FST", "a/b c/d.e", "data/Redux update/x", "Pilóto/測試.txt"})
    {
        CHECK_EQ(MCContentList::PathProblem(good), std::string());
    }

    for (const std::string_view bad :
         {"", "/etc/passwd", "../up", "data/../../up", "data//x", "data/./x", "data/", "C:/x", "data\\x", "a?b",
          "data/con", "data/LPT1.txt", "data/aux.fit", "data/x.", "data/x ", "Redux update/installed.json",
          "redux BACKUP/MCX.EXE", "tab\there", "Redux update"})
    {
        CHECK(!MCContentList::PathProblem(bad).empty());
    }

    CHECK(MCContentList::PathProblem("data/com10").empty());
    CHECK(MCContentList::PathProblem(std::string(201, 'a')).find("longer") != std::string::npos);

    MCContentList::MCList list{.Format = 1, .Release = 3, .LowestProgram = 540};
    list.Zips.push_back({"base-1.zip", 100, MCSha256::Of("zip")});
    list.Files.push_back(Listed("data/a.fit", "a"));
    const auto read = MCContentList::Read(MCContentList::Write(list));
    REQUIRE(read.has_value());
    CHECK_EQ(read->Files.size(), size_t{1});
    CHECK_EQ(read->LowestProgram, uint64_t{540});

    MCContentList::MCList clash = list;
    clash.Files.push_back(Listed("DATA/A.FIT", "b"));
    const auto clashed = MCContentList::Read(MCContentList::Write(clash));
    REQUIRE(!clashed.has_value());
    CHECK(clashed.error().find("twice") != std::string::npos);

    MCContentList::MCList folderClash = list;
    folderClash.Files.push_back(Listed("Data/A.fit/inner", "b"));
    CHECK(!MCContentList::Read(MCContentList::Write(folderClash)).has_value());

    MCContentList::MCList unlistedZip = list;
    unlistedZip.Files.push_back(Listed("data/b.fit", "b", "base-2.zip"));
    CHECK(!MCContentList::Read(MCContentList::Write(unlistedZip)).has_value());

    MCContentList::MCList badPath = list;
    badPath.Files.push_back(Listed("../escape", "b"));
    CHECK(!MCContentList::Read(MCContentList::Write(badPath)).has_value());
}

TEST_CASE("content plan: kept, patched, fetched, the player's replaced and kept, old files removed, only the zips "
          "needed")
{
    const ScratchFolder scratch("plan");
    const std::filesystem::path& root = scratch.Path();
    // On disk: a file as released, an earlier one with a patch, one without, a mod, a file installed by an earlier
    // release and dropped since, and one dropped that the player changed. "DATA" is spelled as a CD has it.
    WriteText(root / "ART.FST", "art v2");
    WriteText(root / "DATA" / "patched.fit", "patched v1");
    WriteText(root / "DATA" / "fetched.fit", "fetched v1");
    WriteText(root / "DATA" / "mod.fit", "the player's own");
    WriteText(root / "DATA" / "dropped.fit", "dropped v1");
    WriteText(root / "DATA" / "changed.fit", "changed by the player");

    MCInstalled::MCRecord installed;
    installed.Release = 1;
    installed.Sets = GameOnly;
    installed.Files["DATA/DROPPED.FIT"] = {
        .Path = "DATA/dropped.fit", .Set = "game", .Sha256 = MCSha256::Of("dropped v1"), .Size = 10};
    installed.Files["DATA/CHANGED.FIT"] = {
        .Path = "DATA/changed.fit", .Set = "game", .Sha256 = MCSha256::Of("changed v1"), .Size = 10};

    MCContentList::MCList list{.Format = 1, .Release = 2, .LowestProgram = 540};
    list.Zips = {{"base-1.zip", 1000, MCSha256::Of("z1")},
                 {"base-2.zip", 2000, MCSha256::Of("z2")},
                 {"update-2.zip", 300, MCSha256::Of("z3")},
                 {"hd-1.zip", 5000, MCSha256::Of("z4")}};
    list.Files.push_back(Listed("ART.FST", "art v2", "base-1.zip"));
    MCContentList::MCFile patched = Listed("data/patched.fit", "patched v2", "update-2.zip");
    patched.Patches.push_back({MCSha256::Of("patched v1"), "patches/patched-1-2.hdiff", 40, MCSha256::Of("p")});
    list.Files.push_back(patched);
    list.Files.push_back(Listed("data/fetched.fit", "fetched v2", "update-2.zip"));
    list.Files.push_back(Listed("data/mod.fit", "mod v2", "base-2.zip"));
    list.Files.push_back(Listed("data/missions/new.fit", "new", "update-2.zip"));
    list.Files.push_back(Listed("HD/terrain.hda", "terrain", "hd-1.zip", std::string(MCContentList::HdSet)));
    const MCSha256::MCDigest listHash = MCSha256::Of("list 2");

    MCContentFolder folder(root);
    const auto plan = MCContentPlan::Make(folder, installed, list, listHash, GameOnly);
    REQUIRE(plan.has_value());
    CHECK(ActionFor(*plan, "ART.FST") == MCAction::Keep);
    CHECK(ActionFor(*plan, "data/patched.fit") == MCAction::Patch);
    CHECK(ActionFor(*plan, "data/fetched.fit") == MCAction::Fetch);
    CHECK(ActionFor(*plan, "data/mod.fit") == MCAction::Fetch);
    CHECK(ActionFor(*plan, "data/missions/new.fit") == MCAction::Fetch);
    CHECK(ActionFor(*plan, "DATA/dropped.fit") == MCAction::Remove);
    CHECK(ActionFor(*plan, "DATA/changed.fit") == MCAction::Remove);
    CHECK(!ActionFor(*plan, "HD/terrain.hda").has_value());
    CHECK(plan->Changes());

    // Kept in Redux backup: every file replaced or removed that an update didn't place as it is (the folder's own, the
    // mod, the one the player changed); the file the earlier release placed goes.
    CHECK(StepFor(*plan, "data/patched.fit")->Keep == MCKeep::Always);
    CHECK(StepFor(*plan, "data/fetched.fit")->Keep == MCKeep::Always);
    CHECK(StepFor(*plan, "data/mod.fit")->Keep == MCKeep::Always);
    CHECK(StepFor(*plan, "DATA/changed.fit")->Keep == MCKeep::Always);
    CHECK(StepFor(*plan, "DATA/dropped.fit")->Keep == MCKeep::IfChanged);

    // Files go where the folders are on disk; the patch is the one from the copy found.
    CHECK_EQ(StepFor(*plan, "data/fetched.fit")->OnDisk, std::string("DATA/fetched.fit"));
    CHECK_EQ(StepFor(*plan, "data/missions/new.fit")->OnDisk, std::string("DATA/missions/new.fit"));
    CHECK_EQ(StepFor(*plan, "data/patched.fit")->Patch->Name, std::string("patches/patched-1-2.hdiff"));

    // Only the zips holding a fetched file: base-1.zip holds only what's kept; hd-1.zip a set not chosen.
    REQUIRE_EQ(plan->Zips.size(), size_t{2});
    CHECK_EQ(plan->Zips[0]->Name, std::string("base-2.zip"));
    CHECK_EQ(plan->Zips[1]->Name, std::string("update-2.zip"));
    CHECK_EQ(plan->DownloadBytes, uint64_t{2000 + 300 + 40});
    CHECK_EQ(plan->StagedBytes, uint64_t{std::string_view("patched v2").size() + std::string_view("fetched v2").size() +
                                         std::string_view("mod v2").size() + std::string_view("new").size()});

    // The record: every listed file the release's as it'll be, placed by the update, but the one found as released.
    CHECK_EQ(plan->Record.Release, uint64_t{2});
    CHECK(plan->Record.List == listHash);
    CHECK(plan->Record.Files.at("DATA/PATCHED.FIT").Sha256 == MCSha256::Of("patched v2"));
    CHECK(plan->Record.Files.at("DATA/MOD.FIT").Sha256 == MCSha256::Of("mod v2"));
    CHECK(plan->Record.Files.at("DATA/MOD.FIT").Placed);
    CHECK(!plan->Record.Files.at("ART.FST").Placed);
    CHECK(!plan->Record.Files.contains("DATA/DROPPED.FIT"));
    CHECK(!plan->Record.Files.contains("DATA/CHANGED.FIT"));

    // With HD chosen, its file is fetched from its zip too.
    MCContentFolder again(root);
    const auto withHd = MCContentPlan::Make(again, installed, list, listHash, GameAndHd);
    REQUIRE(withHd.has_value());
    CHECK(ActionFor(*withHd, "HD/terrain.hda") == MCAction::Fetch);
    CHECK_EQ(withHd->Zips.size(), size_t{3});
}

TEST_CASE("content plan: a file as recorded keeps its hash; one changed since is hashed; HD dropped is removed")
{
    const ScratchFolder scratch("plan-record");
    const std::filesystem::path& root = scratch.Path();
    WriteText(root / "ART.FST", "art v2");
    WriteText(root / "HD" / "terrain.hda", "terrain v1");
    MCContentList::MCList list{.Format = 1, .Release = 2};
    list.Zips = {{"base-1.zip", 1000, MCSha256::Of("z1")}, {"hd-1.zip", 5000, MCSha256::Of("z2")}};
    list.Files.push_back(Listed("ART.FST", "art v2", "base-1.zip"));
    list.Files.push_back(Listed("HD/terrain.hda", "terrain v1", "hd-1.zip", std::string(MCContentList::HdSet)));

    MCContentFolder folder(root);
    const auto first = MCContentPlan::Make(folder, {}, list, MCSha256::Of("list"), GameAndHd);
    REQUIRE(first.has_value());
    CHECK(!first->Changes());

    // The record read back as written.
    const auto record = MCInstalled::FromText(MCInstalled::Text(first->Record));
    REQUIRE(record.has_value());
    CHECK_EQ(record->Files.size(), size_t{2});
    CHECK(record->Sets == GameAndHd);
    CHECK_EQ(record->Files.at("ART.FST").Written, first->Record.Files.at("ART.FST").Written);

    // Bytes changed behind the record's back but size and time put back: the recorded hash is trusted, so it stays.
    const std::filesystem::path art = root / "ART.FST";
    const auto written = std::filesystem::last_write_time(art);
    WriteText(art, "art v3");
    std::filesystem::last_write_time(art, written);
    MCContentFolder second(root);
    const auto trusted = MCContentPlan::Make(second, *record, list, MCSha256::Of("list"), GameAndHd);
    REQUIRE(trusted.has_value());
    CHECK(ActionFor(*trusted, "ART.FST") == MCAction::Keep);

    // Its time moved on: it's hashed, and found not to be the release's: fetched again, the one found kept in Redux
    // backup (found in place, no update placed it).
    CHECK(!record->Files.at("ART.FST").Placed);
    std::filesystem::last_write_time(art, written + std::chrono::seconds(5));
    MCContentFolder third(root);
    const auto rehashed = MCContentPlan::Make(third, *record, list, MCSha256::Of("list"), GameAndHd);
    REQUIRE(rehashed.has_value());
    CHECK(ActionFor(*rehashed, "ART.FST") == MCAction::Fetch);
    CHECK(StepFor(*rehashed, "ART.FST")->Keep == MCKeep::Always);

    // HD removed from the copy's sets: its files go, deleted though found in place (GET downloads them again).
    MCContentFolder fourth(root);
    const auto dropped = MCContentPlan::Make(fourth, *record, list, MCSha256::Of("list"), GameOnly);
    REQUIRE(dropped.has_value());
    CHECK(ActionFor(*dropped, "HD/terrain.hda") == MCAction::Remove);
    CHECK(StepFor(*dropped, "HD/terrain.hda")->Keep == MCKeep::Never);
    CHECK(!dropped->Record.Sets.contains(std::string(MCContentList::HdSet)));
}

TEST_CASE("content plan: a folder or link in a file's place stops the plan, naming it")
{
    const ScratchFolder scratch("plan-blocked");
    const std::filesystem::path& root = scratch.Path();
    std::filesystem::create_directories(root / "data" / "a.fit");
    MCContentList::MCList list{.Format = 1, .Release = 2};
    list.Zips = {{"base-1.zip", 1000, MCSha256::Of("z1")}};
    list.Files.push_back(Listed("data/a.fit", "a", "base-1.zip"));
    MCContentFolder folder(root);
    const auto blocked = MCContentPlan::Make(folder, {}, list, MCSha256::Of("list"), GameOnly);
    REQUIRE(!blocked.has_value());
    CHECK(blocked.error().find("in its way") != std::string::npos);

#ifndef _WIN32
    // Windows makes links only for administrators or in developer mode: the Mac checks a link along the way.
    std::filesystem::remove_all(root / "data");
    std::filesystem::create_directories(root / "elsewhere");
    std::filesystem::create_directory_symlink(root / "elsewhere", root / "data");
    MCContentFolder linked(root);
    const auto link = MCContentPlan::Make(linked, {}, list, MCSha256::Of("list"), GameOnly);
    REQUIRE(!link.has_value());
    CHECK(link.error().find("link") != std::string::npos);
#endif
}

TEST_CASE("content plan: a folder that can't be listed stops the plan, naming it; one that isn't there holds nothing")
{
    const ScratchFolder scratch("plan-unlisted");
    const std::filesystem::path& root = scratch.Path();
    WriteText(root / "data" / "a.fit", "a");
    WriteText(root / "data" / "dropped.fit", "dropped");
    MCContentList::MCList list{.Format = 1, .Release = 2};
    list.Zips = {{"base-1.zip", 1000, MCSha256::Of("z1")}};
    list.Files.push_back(Listed("data/a.fit", "a", "base-1.zip"));
    MCInstalled::MCRecord installed;
    installed.Files["DATA/DROPPED.FIT"] = {
        .Path = "data/dropped.fit", .Set = "game", .Sha256 = MCSha256::Of("dropped"), .Size = 7};

#ifndef _WIN32
    // The Mac stands in for any listing that fails: a folder this account can't read.
    const std::filesystem::path data = root / "data";
    std::filesystem::permissions(data, std::filesystem::perms::none);
    MCContentFolder folder(root);
    const auto unlisted = MCContentPlan::Make(folder, installed, list, MCSha256::Of("list"), GameOnly);
    MCContentList::MCList recordedOnly = list;
    recordedOnly.Files.clear();
    const auto removal = MCContentPlan::Make(folder, installed, recordedOnly, MCSha256::Of("list"), GameOnly);
    std::filesystem::permissions(data, std::filesystem::perms::owner_all);
    REQUIRE(!unlisted.has_value());
    CHECK(unlisted.error().find("can't be listed") != std::string::npos);
    CHECK(unlisted.error().find(MCFileSystem::ToUtf8(data)) != std::string::npos);
    REQUIRE(!removal.has_value());
    CHECK(removal.error().find("can't be listed") != std::string::npos);

    // The failed listing isn't kept: listed again, the files are there.
    const auto listed = MCContentPlan::Make(folder, installed, list, MCSha256::Of("list"), GameOnly);
    REQUIRE(listed.has_value());
    CHECK(ActionFor(*listed, "data/a.fit") == MCAction::Keep);
    CHECK(ActionFor(*listed, "data/dropped.fit") == MCAction::Remove);
#endif

    // A game folder that isn't there: every file is fetched, placed as listed.
    MCContentFolder missing(root / "not-there");
    const auto fresh = MCContentPlan::Make(missing, {}, list, MCSha256::Of("list"), GameOnly);
    REQUIRE(fresh.has_value());
    CHECK(ActionFor(*fresh, "data/a.fit") == MCAction::Fetch);
    CHECK_EQ(StepFor(*fresh, "data/a.fit")->OnDisk, std::string("data/a.fit"));
}
