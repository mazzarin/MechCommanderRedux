#include "stdafx.h"
#include "update/MCReleaseFile.h"
#include "update/MCUpdateJson.h"

#include <charconv>

namespace
{
    using namespace MCUpdateJson;
    using namespace MCReleaseFile;

    constexpr std::string_view FormatKey = "format";
    constexpr std::string_view IssueKey = "issue";
    constexpr std::string_view ReleaseKeysKey = "releaseKeys";
    constexpr std::string_view LowestReleaseKey = "lowestRelease";
    constexpr std::string_view CurrentKey = "current";
    constexpr std::string_view RolloutKey = "rollout";
    constexpr std::string_view ShareKey = "share";
    constexpr std::string_view ReleaseKey = "release";
    constexpr std::string_view VersionKey = "version";
    constexpr std::string_view NotesKey = "notes";
    constexpr std::string_view ContentKey = "content";
    constexpr std::string_view ListKey = "list";
    constexpr std::string_view SizeKey = "size";
    constexpr std::string_view ProgramsKey = "programs";
    constexpr std::string_view BuildKey = "build";
    constexpr std::string_view Sha256Key = "sha256";
    constexpr std::string_view UrlKey = "url";
    constexpr std::string_view SilentKey = "silent";
    constexpr std::string_view KeyListIssueKey = "keyListIssue";
    constexpr std::string_view ReleaseIssueKey = "releaseIssue";
    constexpr std::string_view RolloutNumberKey = "rolloutNumber";
    constexpr std::string_view SkippedKey = "skipped";
    constexpr std::string_view EpochKey = "epoch";
    constexpr std::string_view EdSignaturesKey = "edSignatures";
    constexpr std::string_view MirrorsKey = "mirrors";
    /// <summary>A rollout's share runs from none to every copy.</summary>
    constexpr double NoShare = 0.0;
    constexpr double WholeShare = 100.0;
    /// <summary>The count of rollout numbers (2^32): a number below share/100 of it takes the rollout.</summary>
    constexpr double RolloutNumbers = 4294967296.0;
    /// <summary>The most release keys a list may name.</summary>
    constexpr size_t MaxReleaseKeys = 8;

    uint64_t FormatOf(const MCJson& json)
    {
        const uint64_t format = Count(json, FormatKey);

        if (format == 0)
        {
            Fail("\"{}\" is 0", FormatKey);
        }

        return format;
    }

    /// <summary>The Ed25519 signature the base64 <paramref name="text"/> spells, for <paramref name="platform"/>'s program.</summary>
    MCEdSignature EdSignatureOf(std::string_view text, std::string_view platform)
    {
        const std::vector<uint8_t> bytes = Base64(text, EdSignaturesKey);

        if (bytes.size() != EdSignatureBytes)
        {
            Fail("the program for \"{}\" has a signature of {} bytes, not {}", platform, bytes.size(),
                 EdSignatureBytes);
        }

        MCEdSignature signature{};
        std::ranges::copy(bytes, signature.begin());
        return signature;
    }

    MCRelease ReadRelease(const MCJson& json)
    {
        MCRelease release;
        release.Number = Count(json, ReleaseKey);
        release.Version = Text(json, VersionKey);
        release.Notes = TextOr(json, NotesKey, "");
        const MCJson& content = Child(json, ContentKey);
        release.List = Digest(content, ListKey);
        release.ListSize = Count(content, SizeKey);

        for (const auto& [platform, entry] : Child(json, ProgramsKey).items())
        {
            if (!entry.is_object())
            {
                Fail("the program for \"{}\" isn't an object", platform);
            }

            MCProgram program;
            program.Build = Count(entry, BuildKey);
            program.Size = Count(entry, SizeKey);

            if (entry.contains(Sha256Key))
            {
                program.Sha256 = Digest(entry, Sha256Key);
            }

            program.Url = TextOr(entry, UrlKey, "");
            program.Silent = TextOr(entry, SilentKey, "");

            for (const MCJson& signature : Array(entry, EdSignaturesKey, false))
            {
                if (!signature.is_string() || program.EdSignatures.size() == MaxReleaseKeys)
                {
                    Fail("the program for \"{}\" has signatures that aren't up to {} strings", platform,
                         MaxReleaseKeys);
                }

                program.EdSignatures.push_back(EdSignatureOf(signature.get_ref<const std::string&>(), platform));
            }

            release.Programs.emplace(platform, std::move(program));
        }

        return release;
    }

    /// <summary>
    /// <paramref name="json"/>'s mirrors (<paramref name="what"/> says whose): none without the key, else up to
    /// MaxMirrors strings. Whether each is an address a source takes is the source's to say (update/MCHttpSource.h).
    /// </summary>
    std::vector<std::string> MirrorsOf(const MCJson& json, std::string_view what)
    {
        if (!json.contains(MirrorsKey))
        {
            return {};
        }

        std::vector<std::string> mirrors = Strings(json, MirrorsKey);

        if (mirrors.size() > MaxMirrors)
        {
            Fail("{} names {} mirrors (at most {})", what, mirrors.size(), MaxMirrors);
        }

        return mirrors;
    }

    MCJson WriteRelease(const MCRelease& release)
    {
        MCJson programs = MCJson::object();

        for (const auto& [platform, program] : release.Programs)
        {
            MCJson entry{{BuildKey, program.Build}, {SizeKey, program.Size}};

            if (program.Sha256)
            {
                entry[std::string(Sha256Key)] = MCSha256::Hex(*program.Sha256);
            }

            if (!program.Url.empty())
            {
                entry[std::string(UrlKey)] = program.Url;
            }

            if (!program.Silent.empty())
            {
                entry[std::string(SilentKey)] = program.Silent;
            }

            if (!program.EdSignatures.empty())
            {
                MCJson signatures = MCJson::array();

                for (const MCEdSignature& signature : program.EdSignatures)
                {
                    signatures.push_back(ToBase64(signature));
                }

                entry[std::string(EdSignaturesKey)] = std::move(signatures);
            }

            programs[platform] = std::move(entry);
        }

        return MCJson{{ReleaseKey, release.Number},
                      {VersionKey, release.Version},
                      {NotesKey, release.Notes},
                      {ContentKey, {{ListKey, MCSha256::Hex(release.List)}, {SizeKey, release.ListSize}}},
                      {ProgramsKey, std::move(programs)}};
    }
}

std::expected<MCKeyList, std::string> MCReleaseFile::ReadKeyList(std::string_view payload)
{
    return Read("the key list",
                [&]()
                {
                    const MCJson json = Object(payload);
                    MCKeyList keys;
                    keys.Format = FormatOf(json);
                    keys.Issue = Count(json, IssueKey);
                    keys.LowestRelease = Count(json, LowestReleaseKey);

                    if (json.contains(EpochKey))
                    {
                        keys.Epoch = Count(json, EpochKey);
                    }

                    const MCJson& list = Array(json, ReleaseKeysKey);

                    if (list.empty() || list.size() > MaxReleaseKeys)
                    {
                        Fail("names {} release keys (1 to {})", list.size(), MaxReleaseKeys);
                    }

                    for (const MCJson& entry : list)
                    {
                        const std::optional<MCSignedFile::MCPublicKey> key =
                            entry.is_string() ? MCSignedFile::KeyFromBase64(entry.get_ref<const std::string&>())
                                              : std::nullopt;

                        if (!key)
                        {
                            Fail("a release key isn't an Ed25519 public key in base64");
                        }

                        keys.ReleaseKeys.push_back(*key);
                    }

                    return keys;
                });
}

std::expected<MCReleases, std::string> MCReleaseFile::ReadReleases(std::string_view payload)
{
    return Read("release.json",
                [&]()
                {
                    const MCJson json = Object(payload);
                    MCReleases releases;
                    releases.Format = FormatOf(json);
                    releases.Issue = Count(json, IssueKey);
                    releases.Current = ReadRelease(Child(json, CurrentKey));

                    if (json.contains(RolloutKey))
                    {
                        const MCJson& rollout = Child(json, RolloutKey);
                        releases.Rollout = ReadRelease(rollout);
                        releases.RolloutShare = Number(rollout, ShareKey);

                        if (!(releases.RolloutShare >= NoShare && releases.RolloutShare <= WholeShare))
                        {
                            Fail("the rollout's share is {} (0 to {})", releases.RolloutShare, WholeShare);
                        }

                        if (releases.Rollout->Number <= releases.Current.Number)
                        {
                            Fail("the rollout's release ({}) isn't newer than the current one ({})",
                                 releases.Rollout->Number, releases.Current.Number);
                        }
                    }

                    releases.Mirrors = MirrorsOf(json, "release.json");
                    return releases;
                });
}

std::expected<MCTrusted, std::string> MCReleaseFile::Check(std::string_view keyList, std::string_view releases,
                                                           std::span<const MCSignedFile::MCPublicKey> rootKeys,
                                                           const MCKept& kept, uint64_t lowestKeyListIssue)
{
    const auto keyPayload = MCSignedFile::Open(keyList, MCSignedFile::KeyListType, rootKeys);

    if (!keyPayload)
    {
        return std::unexpected("the key list: " + keyPayload.error());
    }

    auto keys = ReadKeyList(*keyPayload);

    if (!keys)
    {
        return std::unexpected(keys.error());
    }

    if (keys->Issue < kept.KeyListIssue)
    {
        return std::unexpected(
            std::format("the key list is issue {}, older than issue {} read before", keys->Issue, kept.KeyListIssue));
    }

    if (keys->Issue < lowestKeyListIssue)
    {
        return std::unexpected(std::format("the key list is issue {}, older than issue {}, the lowest this build takes",
                                           keys->Issue, lowestKeyListIssue));
    }

    if (keys->Epoch < kept.Epoch)
    {
        return std::unexpected(
            std::format("the key list is epoch {}, older than epoch {} read before", keys->Epoch, kept.Epoch));
    }

    const auto releasePayload = MCSignedFile::Open(releases, MCSignedFile::ReleaseType, keys->ReleaseKeys);

    if (!releasePayload)
    {
        return std::unexpected("release.json: " + releasePayload.error());
    }

    auto read = ReadReleases(*releasePayload);

    if (!read)
    {
        return std::unexpected(read.error());
    }

    // A later epoch starts release.json's issues again: a release key that signed one too high to outbid is put right.
    if (const uint64_t lowestIssue = keys->Epoch > kept.Epoch ? 0 : kept.ReleaseIssue; read->Issue < lowestIssue)
    {
        return std::unexpected(
            std::format("release.json is issue {}, older than issue {} read before", read->Issue, lowestIssue));
    }

    if (read->Current.Number < keys->LowestRelease)
    {
        return std::unexpected(std::format("release.json's release {} is below the key list's lowest, {}",
                                           read->Current.Number, keys->LowestRelease));
    }

    read->Current.Epoch = keys->Epoch;

    if (read->Rollout)
    {
        read->Rollout->Epoch = keys->Epoch;
    }

    return MCTrusted{std::move(*keys), std::move(*read)};
}

MCKept MCReleaseFile::Keep(const MCKept& kept, const MCTrusted& trusted)
{
    MCKept taken = kept;

    if (trusted.Keys.Epoch > kept.Epoch)
    {
        // The earlier epoch's issue and skip are of files and numbers that no longer count.
        taken.Epoch = trusted.Keys.Epoch;
        taken.ReleaseIssue = 0;
        taken.Skipped = 0;
    }

    taken.KeyListIssue = std::max(kept.KeyListIssue, trusted.Keys.Issue);
    taken.ReleaseIssue = std::max(taken.ReleaseIssue, trusted.Releases.Issue);
    taken.Mirrors = trusted.Releases.Mirrors;
    return taken;
}

MCOrder MCReleaseFile::OrderOf(const MCRelease& release)
{
    return {.Epoch = release.Epoch, .Release = release.Number};
}

std::expected<MCKept, std::string> MCReleaseFile::ReadKept(std::string_view text)
{
    return Read(KeptFile,
                [&]
                {
                    const MCJson json = Object(text);
                    MCKept kept;
                    kept.KeyListIssue = Count(json, KeyListIssueKey);
                    kept.ReleaseIssue = Count(json, ReleaseIssueKey);

                    if (json.contains(EpochKey))
                    {
                        kept.Epoch = Count(json, EpochKey);
                    }

                    if (json.contains(RolloutNumberKey))
                    {
                        const uint64_t number = Count(json, RolloutNumberKey);

                        if (number > std::numeric_limits<uint32_t>::max())
                        {
                            Fail("\"{}\" is past 32 bits", RolloutNumberKey);
                        }

                        kept.RolloutNumber = static_cast<uint32_t>(number);
                    }

                    if (json.contains(SkippedKey))
                    {
                        kept.Skipped = Count(json, SkippedKey);
                    }

                    kept.Mirrors = MirrorsOf(json, KeptFile);
                    return kept;
                });
}

std::string MCReleaseFile::KeptText(const MCKept& kept)
{
    MCJson json{{KeyListIssueKey, kept.KeyListIssue}, {ReleaseIssueKey, kept.ReleaseIssue}, {EpochKey, kept.Epoch}};

    if (kept.RolloutNumber)
    {
        json[std::string(RolloutNumberKey)] = *kept.RolloutNumber;
    }

    if (kept.Skipped != 0)
    {
        json[std::string(SkippedKey)] = kept.Skipped;
    }

    if (!kept.Mirrors.empty())
    {
        json[std::string(MirrorsKey)] = kept.Mirrors;
    }

    return json.dump(2);
}

std::string MCReleaseFile::FeedItemProblem(const MCProgram& held, std::string_view runningBuild, const MCFeedItem& item)
{
    if (item.Build != std::to_string(held.Build))
    {
        return std::format("it's build {}, not {}, the one release.json gives", item.Build, held.Build);
    }

    uint64_t running = 0;

    if (const auto [end, error] =
            std::from_chars(runningBuild.data(), runningBuild.data() + runningBuild.size(), running);
        runningBuild.empty() || error != std::errc() || end != runningBuild.data() + runningBuild.size())
    {
        return std::format("the running build, \"{}\", isn't a number", runningBuild);
    }

    if (held.Build <= running)
    {
        return std::format("build {} isn't newer than build {}, the one running", held.Build, running);
    }

    if (item.Deltas != 0 || item.Delta)
    {
        return "it comes with delta updates, which release.json doesn't sign for";
    }

    if (held.EdSignatures.empty())
    {
        return "release.json gives no signature for the Mac's program";
    }

    if (const auto signature = Read(EdSignaturesKey, [&] { return EdSignatureOf(item.EdSignature, MacPlatform); });
        !signature || std::ranges::find(held.EdSignatures, *signature) == held.EdSignatures.end())
    {
        return "its archive's signature isn't one release.json gives";
    }

    if (item.Size != held.Size)
    {
        return std::format("its archive is {} bytes, not {}, the size release.json gives", item.Size, held.Size);
    }

    return {};
}

const MCRelease& MCReleaseFile::For(const MCReleases& releases, uint32_t rolloutNumber)
{
    if (releases.Rollout && static_cast<double>(rolloutNumber) < releases.RolloutShare / WholeShare * RolloutNumbers)
    {
        return *releases.Rollout;
    }

    return releases.Current;
}

std::string MCReleaseFile::KeyListPayload(const MCKeyList& keys)
{
    MCJson list = MCJson::array();

    for (const MCSignedFile::MCPublicKey& key : keys.ReleaseKeys)
    {
        list.push_back(ToBase64(key));
    }

    return MCJson{{FormatKey, keys.Format},
                  {IssueKey, keys.Issue},
                  {ReleaseKeysKey, std::move(list)},
                  {LowestReleaseKey, keys.LowestRelease},
                  {EpochKey, keys.Epoch}}
        .dump(2);
}

std::string MCReleaseFile::ReleasesPayload(const MCReleases& releases)
{
    MCJson json{{FormatKey, releases.Format}, {IssueKey, releases.Issue}, {CurrentKey, WriteRelease(releases.Current)}};

    if (releases.Rollout)
    {
        MCJson rollout = WriteRelease(*releases.Rollout);
        rollout[std::string(ShareKey)] = releases.RolloutShare;
        json[std::string(RolloutKey)] = std::move(rollout);
    }

    if (!releases.Mirrors.empty())
    {
        json[std::string(MirrorsKey)] = releases.Mirrors;
    }

    return json.dump(2);
}
