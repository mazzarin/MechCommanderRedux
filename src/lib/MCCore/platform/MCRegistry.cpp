#include "stdafx.h"
#include "platform/MCRegistry.h"
#include "lib/MCThreadKeeper.h"
#include "platform/MCFileSystem.h"
#include "platform/MCNeverDestroyed.h"
#include "platform/MCUtf8Path.h"

#include <condition_variable>

namespace
{
    /// <summary>The store's file in the user folder.</summary>
    constexpr std::string_view StoreFileName = "registry.cfg";

    /// <summary>Every stored value, keyed by <see cref="StoreKey"/>.</summary>
    using MCValues = std::map<std::string, std::string>;

    /// <summary>A key and value name as one lower-case store key (<c>key|value</c>).</summary>
    std::string StoreKey(std::string_view keyName, std::string_view valueName)
    {
        std::string key;
        key.reserve(keyName.size() + valueName.size() + 1);

        for (char c : keyName)
        {
            key.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }

        key.push_back('|');

        for (char c : valueName)
        {
            key.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }

        return key;
    }

    /// <summary>The values in the store's file; each line of the file is <c>key=data</c>.</summary>
    MCValues LoadStore()
    {
        MCValues values;
        std::ifstream file(MCFileSystem::Resolve(StoreFileName));
        std::string line;

        while (std::getline(file, line))
        {
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }

            const size_t equals = line.find('=');

            if (equals != std::string::npos)
            {
                values[line.substr(0, equals)] = line.substr(equals + 1);
            }
        }

        return values;
    }

    /// <summary>The file's text holding <paramref name="values"/>.</summary>
    std::string StoreText(const MCValues& values)
    {
        std::string text;

        for (const auto& [key, value] : values)
        {
            text += key;
            text += '=';
            text += value;
            text += '\n';
        }

        return text;
    }

    /// <summary>Writes <paramref name="text"/> to the file <paramref name="path"/>, logging a failure.</summary>
    void SaveStore(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream file(path, std::ios::trunc);

        if (file)
        {
            file << text;
            file.close();
        }

        if (!file)
        {
            SDL_Log("MCRegistry: couldn't save %s (%zu bytes)", MCFileSystem::ToUtf8(path).c_str(), text.size());
        }
    }

    /// <summary>
    /// The values as last read or written, for the folders they were read for, and the saves the saver's thread has
    /// still to write, under Lock.
    /// </summary>
    struct MCStore
    {
        std::mutex Lock;
        /// <summary>Told when the saver ends and when a hold ends.</summary>
        std::condition_variable Changed;
        /// <summary>Whether Values holds the store of UserRoot and GameRoot.</summary>
        bool Loaded = false;
        std::filesystem::path UserRoot;
        std::filesystem::path GameRoot;
        MCValues Values;
        /// <summary>Each file still to write and the text it's to hold: the newest only.</summary>
        std::map<std::filesystem::path, std::string> Saves;
        /// <summary>Whether the saver's thread runs; it ends once no save is left.</summary>
        bool Saving = false;
        /// <summary>The tests' holds alive (MCSavesHeld): the saver waits while there are any.</summary>
        size_t Holds = 0;
    };

    /// <summary>The process's store: never destroyed, as the saver may write while the process ends.</summary>
    MCStore& TheStore()
    {
        static MCNeverDestroyed<MCStore> store;
        return *store;
    }

    /// <summary>
    /// The store's values for the folders set now (<paramref name="lock"/> holding the store's lock), read from the
    /// file when they aren't the ones read last, once the saves of the ones before are written.
    /// </summary>
    MCValues& ValuesNow(MCStore& store, std::unique_lock<std::mutex>& lock)
    {
        const std::filesystem::path& userRoot = MCFileSystem::UserRoot();
        const std::filesystem::path& gameRoot = MCFileSystem::GameRoot();

        if (!store.Loaded || store.UserRoot != userRoot || store.GameRoot != gameRoot)
        {
            store.Changed.wait(lock, [&store] { return !store.Saving; });
            store.Values = LoadStore();
            store.UserRoot = userRoot;
            store.GameRoot = gameRoot;
            store.Loaded = true;
        }

        return store.Values;
    }

    /// <summary>The saver's thread: writes the saves asked for, the newest of each file, until none is left.</summary>
    void Save()
    {
        MCStore& store = TheStore();
        std::unique_lock lock(store.Lock);

        for (;;)
        {
            store.Changed.wait(lock, [&store] { return store.Holds == 0; });

            if (store.Saves.empty())
            {
                store.Saving = false;
                store.Changed.notify_all();
                return;
            }

            const std::map<std::filesystem::path, std::string> saves = std::exchange(store.Saves, {});
            lock.unlock();

            for (const auto& [path, text] : saves)
            {
                SaveStore(path, text);
            }

            lock.lock();
        }
    }
}

namespace MCRegistry
{
    std::optional<std::string> Read(std::string_view keyName, std::string_view valueName)
    {
        MCStore& store = TheStore();
        std::unique_lock lock(store.Lock);
        const MCValues& values = ValuesNow(store, lock);
        const auto found = values.find(StoreKey(keyName, valueName));

        if (found == values.end())
        {
            return std::nullopt;
        }

        return found->second;
    }

    void Write(std::string_view keyName, std::string_view valueName, std::string_view data)
    {
        MCStore& store = TheStore();
        std::unique_lock lock(store.Lock);
        MCValues& values = ValuesNow(store, lock);
        const std::string key = StoreKey(keyName, valueName);

        if (const auto found = values.find(key); found != values.end() && found->second == data)
        {
            return;
        }

        values[key] = std::string(data);
        // The file found here, by the folders set now: the saver's thread only writes it.
        store.Saves[MCFileSystem::ResolveWrite(StoreFileName)] = StoreText(values);

        if (!store.Saving)
        {
            store.Saving = true;
            MCThreadKeeper::Process().Keep(std::thread(Save));
        }
    }

    void WaitForSaves()
    {
        MCStore& store = TheStore();
        std::unique_lock lock(store.Lock);
        store.Changed.wait(lock, [&store] { return !store.Saving; });
    }

    MCSavesHeld::MCSavesHeld()
    {
        MCStore& store = TheStore();
        const std::scoped_lock lock(store.Lock);
        store.Holds++;
    }

    MCSavesHeld::~MCSavesHeld()
    {
        MCStore& store = TheStore();
        const std::scoped_lock lock(store.Lock);
        store.Holds--;
        store.Changed.notify_all();
    }
}
