#include "StdAfx.h"
#include "IgnoreListStorage.h"
#include "../System/ChunklessSaver.h"
#include "../System/Crc32Checksum.h"
#include "../System/FileSystem/FileStream.h"
#include "../System/FileSystem/FileUtils.h"

#include "System/InlineProfiler.h"

#define _ULL(x) static_cast<unsigned long long>((x))

namespace
{
  const unsigned long long _IgnoreListMagic =
    (_ULL(0xFF) << 56) |
    (_ULL(0xFF) << 48) |
    (_ULL('E')  << 40) |
    (_ULL('R')  << 32) |
    (_ULL('O')  << 24) |
    (_ULL('N')  << 16) |
    (_ULL('G')  <<  8) |
    (_ULL('I')  <<  0);
}

#define _IGNORE_LIST_VERSIONLESS (0ULL)
#define _IGNORE_LIST_V1 (1ULL)
#define _IGNORE_LIST_V2 (2ULL)
#define _IGNORE_LIST_V3 (3ULL)

#define IGNORE_LIST_MAGIC _IgnoreListMagic
#define IGNORE_LIST_VERSION _IGNORE_LIST_V3

namespace
{
  using namespace NGameX::IgnoreList;

  namespace EIgnoreListStorageFormat
  {
    enum Enum
    {
      Legacy,
      Regular,
    };
  }

  struct IgnoreListHeader
  {
    unsigned long long magic;
    unsigned long long version;
  };

  static nstl::string MakeIgnoreListFileName(const TUserId& _auid, const EIgnoreListStorageFormat::Enum format)
  {
    switch (format)
    {
    case EIgnoreListStorageFormat::Legacy:
      return NStr::StrFmt("ignorelist%llu.dat", _auid);
    case EIgnoreListStorageFormat::Regular:
      return NStr::StrFmt("ignore.ignore");
    default:
      return NStr::StrFmt("~.ignore");
    }
  }

  static nstl::string MakeIgnoreListFilePath(const TUserId& _auid, const EIgnoreListStorageFormat::Enum format)
  {
    const nstl::string filename(MakeIgnoreListFileName(_auid, format));

    return NProfile::GetFullFilePath(filename, NProfile::FOLDER_USER);
  }

  class LegacyIgnoreListReaderImpl : public NonCopyable
  {
  public:
    explicit LegacyIgnoreListReaderImpl(const nstl::string& _path)
      : path(_path)
    {

    }

    bool operator()(TUserSet& users, TUserNicknameMap& nicknames) const
    {
      const CObj<FileStream> pStream(new FileStream(path, FILEACCESS_READ, FILEOPEN_OPEN_EXISTING));

      if (!IsValid(pStream) || !pStream->IsOk() || !pStream->CanRead() || pStream->GetSize() <= 2)
      {
        MessageTrace("Cannot read ignore list file \"%s\"", path);
        return false;
      }

      wstring szBuffer;

      szBuffer.resize(pStream->GetSize());

      pStream->Read(&(szBuffer[0]), pStream->GetSize());
      pStream->Close();

      MessageTrace("Reading \"%s\"", path);

      typedef vector<wstring> TLines;

      TLines lines;

      NStr::SplitString(szBuffer.c_str() + 1, &lines, L"\r\n"); // ���������� ���� wchar_t � BOM

      TLines::const_iterator it = lines.begin();
      TLines::const_iterator it_end = lines.end();
      for (; nstl::distance(it, it_end) > 1; nstl::advance(it, 2))
      {
        wstring line1(*(it + 0));
        wstring line2(*(it + 1));

        NStr::TrimBoth(line1, L"\n\r");
        NStr::TrimBoth(line2, L"\n\r");

        if (line1.empty() || line2.empty())
          continue;

        const TUserId auid = _wtoi64(line1.c_str());

        if (!auid)
          continue;

        users.insert(auid);
        nicknames.insert(nstl::make_pair(auid, line2));
      }

      return true;
    }
  private:
    LegacyIgnoreListReaderImpl();

    const nstl::string path;
  };

  class IgnoreListReaderImpl : public NonCopyable
  {
  public:
    explicit IgnoreListReaderImpl(const nstl::string& _path)
      : path(_path)
    {

    }

    bool operator()(TUserSet& users, TUserNicknameMap& nicknames)
    {
		MessageTrace("Try Read Impl");
      const CObj<FileStream> pStream(new FileStream(path, FILEACCESS_READ, FILEOPEN_OPEN_EXISTING));

      if (!IsValid(pStream) || !pStream->IsOk() || !pStream->CanRead())
      {
        MessageTrace("Cannot read ignore list file \"%s\"", path);
        return false;
      }

      MessageTrace("Reading \"%s\"", path);

      if (!TryReadHeader(pStream))
      {
        pStream->Seek(0, SEEKORIGIN_BEGIN);
      }

      // Если это новый формат V3 (только никнеймы), читаем по-другому
      if (header.version == _IGNORE_LIST_V3)
      {
        return ReadNicknameOnlyFormat(pStream, users, nicknames);
      }

      // Старый формат с auid и никнеймами
      TUserId userId;
      TUserNickname userNickname;

      while (pStream->GetPosition() < pStream->GetSize())
      {
        (*pStream) >> userId;

        if (!pStream->IsOk())
        {
          ErrorTrace("Read from \"%s\" failed!", path);
          break;
        }

        users.insert(userId);

        if (TryReadNickname(pStream, userNickname))
        {
          if (!userNickname.empty())
            nicknames[userId] = userNickname;
        }
      }

      pStream->Close();

      return true;
    }

    bool HasHeader() const
    {
      return
        (header.magic == IGNORE_LIST_MAGIC) &&
        (header.version != _IGNORE_LIST_VERSIONLESS);
    }
  private:
    IgnoreListReaderImpl();

    bool TryReadHeader(const CObj<FileStream>& stream)
    {
      const int bytesAvailable = (stream->GetSize() - stream->GetPosition());

      if (bytesAvailable < sizeof(IgnoreListHeader))
      {
        return false;
      }

      int bytesRead = 0;

      bytesRead += stream->Read(&header.magic, sizeof(header.magic));
      bytesRead += stream->Read(&header.version, sizeof(header.version));

      if (!stream->IsOk())
      {
        ErrorTrace("Read from \"%s\" failed! [header]", path);

        header.version = _IGNORE_LIST_VERSIONLESS;
        return false;
      }

      if (bytesRead != sizeof(IgnoreListHeader))
      {
        header.version = _IGNORE_LIST_VERSIONLESS;
        return false;
      }

      if (header.magic != IGNORE_LIST_MAGIC)
      {
        header.version = _IGNORE_LIST_VERSIONLESS;
        return false;
      }

      return true;
    }

    bool TryReadNickname(const CObj<FileStream>& stream, TUserNickname& nickname)
    {
      //if (!ShouldReadNicknames())
        //return true;

      unsigned short length;

      (*stream) >> length;

      if (!stream->IsOk())
      {
        ErrorTrace("Read from \"%s\" failed! [nickname:length]", path);
        return false;
      }

      nickname.resize(length);

      void* const _memory = &(nickname[0]);
      int const _capacity = nickname.length() * sizeof(TUserNickname::value_type);

      int const size = stream->Read(_memory, _capacity);

      if (!stream->IsOk() || (size != _capacity))
      {
        ErrorTrace("Read from \"%s\" failed! [nickname:data]", path);
        return false;
      }

      // Удаляем завершающий ноль, если он есть
      if (length > 0 && nickname[length - 1] == 0)
      {
        nickname.resize(length - 1);
      }

	  MessageTrace("Read nickname success: '%s' (original length=%d, final length=%d)", 
                   NStr::ToMBCS(nickname).c_str(), length, nickname.length());

      return true;
    }

    bool ShouldReadNicknames() const
    {
      return (header.version == _IGNORE_LIST_V2) || (header.version == _IGNORE_LIST_V3);
    }

    bool ReadNicknameOnlyFormat(const CObj<FileStream>& pStream, TUserSet& users, TUserNicknameMap& nicknames)
    {
      MessageTrace("Reading nickname-only format");

      // Читаем количество никнеймов
      unsigned int nicknameCount;
      (*pStream) >> nicknameCount;

      if (!pStream->IsOk())
      {
        ErrorTrace("Read nickname count from \"%s\" failed!", path);
        return false;
      }

      // Очищаем старые данные - в новом формате нет auid
      users.clear();
      nicknames.clear();

      // Читаем никнеймы
      for (unsigned int i = 0; i < nicknameCount; ++i)
      {
        TUserNickname nickname;
        if (!TryReadNickname(pStream, nickname))
        {
          ErrorTrace("Read nickname from \"%s\" failed!", path);
          return false;
        }

        if (!nickname.empty())
        {
          // Используем специальный маркер для обозначения нового формата
          nicknames[i + 1] = nickname; // Используем индекс + 1 как временный ключ
        }
      }

      pStream->Close();
      return true;
    }

    const nstl::string path;

    IgnoreListHeader header;
  };

  class IgnoreListWriterImpl : public NonCopyable
  {
  public:
    explicit IgnoreListWriterImpl(const nstl::string& _path)
      : path(_path)
      , header()
      , nicknameSet(0)
    {
      header.magic = IGNORE_LIST_MAGIC;
      header.version = IGNORE_LIST_VERSION;
    }

    // Новый конструктор для сохранения только никнеймов (V3)
    explicit IgnoreListWriterImpl(const nstl::string& _path, const TUserNicknameStringSet& _nicknameSet)
      : path(_path)
      , header()
      , nicknameSet(&_nicknameSet)
    {
      header.magic = IGNORE_LIST_MAGIC;
      header.version = _IGNORE_LIST_V3;
    }

    bool operator()(const TUserSet& users, const TUserNicknameMap& nicknames) const
    {
      if (header.version == _IGNORE_LIST_V3)
      {
        return WriteNicknameOnlyFormat();
      }
      else
      {
        return WriteOldFormat(users, nicknames);
      }
    }
  private:
    IgnoreListWriterImpl();

    bool ShouldWriteHeader() const
    {
      return true;
    }

    bool ShouldWriteNicknames() const
    {
      return (header.version == _IGNORE_LIST_V2) || (header.version == _IGNORE_LIST_V3);
    }

    bool TryWriteNickname(const CObj<FileStream>& stream, const TUserId& userId, const TUserNicknameMap& nicknames) const
    {
      if (!ShouldWriteNicknames())
        return true;

      TUserNicknameMap::const_iterator it = nicknames.find(userId);

      if (it == nicknames.end())
        return WriteNicknameImpl(stream, TUserNickname());
      else
        return WriteNicknameImpl(stream, it->second);
    }

    bool WriteNicknameImpl(const CObj<FileStream>& stream, const TUserNickname& nickname) const
    {
      unsigned short length = nickname.length();

      // terminating zero
      if (length > 0)
        ++length;

      (*stream) << length;

      if (!stream->IsOk())
      {
        ErrorTrace("Write to \"%s\" failed! [nickname:length]", path);
        return false;
      }

      if (length > 0)
      {
        const void* const _memory = &(nickname[0]);
        int const _capacity = (nickname.length() + 1) * sizeof(TUserNickname::value_type);

        int const size = stream->Write(_memory, _capacity);

        if (!stream->IsOk() || (size != _capacity))
        {
          ErrorTrace("Write to \"%s\" failed! [nickname:data]", path);
          return false;
        }
      }

      return true;
    }

    const nstl::string path;

    IgnoreListHeader header;
    const TUserNicknameStringSet* nicknameSet;

    bool WriteOldFormat(const TUserSet& users, const TUserNicknameMap& nicknames) const
    {
      const CObj<FileStream> pStream(new FileStream(path, FILEACCESS_WRITE, FILEOPEN_CREATE_NEW));

      if (!IsValid(pStream) || !pStream->IsOk() || !pStream->CanWrite())
      {
        MessageTrace("Cannot write ignore list file \"%s\"", path);
        return false;
      }

      MessageTrace("Writing \"%s\"", path);

      if (ShouldWriteHeader())
      {
        (*pStream) << header.magic;
        (*pStream) << header.version;
      }

      TUserSet::const_iterator it = users.begin();
      TUserSet::const_iterator it_end = users.end();
      for (; it != it_end; ++it)
      {
        (*pStream) << (*it);

        if (!pStream->IsOk())
        {
          ErrorTrace("Write to \"%s\" failed!", path);
          break;
        }

        if (TryWriteNickname(pStream, *it, nicknames))
        {
          // placeholder
        }
      }

      pStream->Close();

      return true;
    }

    bool WriteNicknameOnlyFormat() const
    {
      const CObj<FileStream> pStream(new FileStream(path, FILEACCESS_WRITE, FILEOPEN_CREATE_NEW));

      if (!IsValid(pStream) || !pStream->IsOk() || !pStream->CanWrite())
      {
        MessageTrace("Cannot write ignore list file \"%s\"", path);
        return false;
      }

      MessageTrace("Writing nicknames only \"%s\"", path);

      // Записываем заголовок
      (*pStream) << header.magic;
      (*pStream) << header.version;

      if (!pStream->IsOk())
      {
        ErrorTrace("Write header to \"%s\" failed!", path);
        return false;
      }

      // Записываем количество никнеймов
      unsigned int nicknameCount = nicknameSet ? nicknameSet->size() : 0;
      (*pStream) << nicknameCount;

      if (!pStream->IsOk())
      {
        ErrorTrace("Write nickname count to \"%s\" failed!", path);
        return false;
      }

      // Записываем никнеймы
      if (nicknameSet)
      {
        for (TUserNicknameStringSet::const_iterator it = nicknameSet->begin(); it != nicknameSet->end(); ++it)
        {
          if (!WriteNicknameImpl(pStream, NStr::ToUnicode(*it)))
          {
            ErrorTrace("Write nickname to \"%s\" failed!", path);
            return false;
          }
        }
      }

      pStream->Close();
      return true;
    }
  };

  class IgnoreListWriter : public NonCopyable
  {
  public:
    explicit IgnoreListWriter(const TUserId& _auid)
      : auid(_auid)
      , nicknameSet(0)
    {

    }

    // Новый конструктор для сохранения только никнеймов
    explicit IgnoreListWriter(const TUserId& _auid, const TUserNicknameStringSet& _nicknameSet)
      : auid(_auid)
      , nicknameSet(&_nicknameSet)
    {

    }

    bool operator()(const TUserSet& users, const TUserNicknameMap& nicknames) const
    {
      const nstl::string path(MakeIgnoreListFilePath(auid, EIgnoreListStorageFormat::Regular));

      if (nicknameSet)
      {
        return IgnoreListWriterImpl(path, *nicknameSet)(users, nicknames);
      }
      else
      {
        return IgnoreListWriterImpl(path)(users, nicknames);
      }
    }
  private:
    IgnoreListWriter();

    const TUserId auid;
    const TUserNicknameStringSet* nicknameSet;
  };

  class IgnoreListReader : public NonCopyable
  {
  public:
    explicit IgnoreListReader(const TUserId& _auid)
      : auid(_auid)
    {

    }

    bool operator()(TUserSet& users, TUserNicknameMap& nicknames) const
    {
      users.clear();
      nicknames.clear();

      {
		  MessageTrace("Try Read");
        const nstl::string path(MakeIgnoreListFilePath(auid, EIgnoreListStorageFormat::Regular));
 MessageTrace("%s", path);
        if (NFile::DoesFileExist(path))
        {
			MessageTrace("File Exists");
          IgnoreListReaderImpl impl(path);

          const bool read = impl(users, nicknames);

          if (read && !impl.HasHeader())
          {
            const IgnoreListWriter writer(auid);
            writer(users, nicknames);
          }

          return read;
        }
      }
  MessageTrace("Try Regular");
      {
        const nstl::string path(MakeIgnoreListFilePath(auid, EIgnoreListStorageFormat::Legacy));
  MessageTrace("%s", path);
        if (NFile::DoesFileExist(path))
        {
				MessageTrace("File Exists");
          const bool read = LegacyIgnoreListReaderImpl(path)(users, nicknames);

          if (read)
          {
            const IgnoreListWriter writer(auid);
            writer(users, nicknames);
          }

          return read;
        }
      }
MessageTrace("End Read");
      return true;
    }
  private:
    IgnoreListReader();

    const TUserId auid;
  };
}

namespace NGameX
{

  IgnoreListStorage::IgnoreListStorage(const TUserId _auid)
    : ownerUserId(_auid)
    , users()
  {
	  MessageTrace("Load %s", _auid);
    LoadFromFile();
  }

  void IgnoreListStorage::AddListener(IIgnoreListListener* const listener)
  {
    if (!listener)
      return;

    // TODO: �� ��������� listener'�, ���� �� ��� ���������������

    listeners.push_back(listener);
  }

  void IgnoreListStorage::AddUser(const TUserNickname& nickname)
  {
    if (nickname.empty())
      return;

    nstl::string nicknameStr = NStr::ToMBCS(nickname);
    if (nicknameStringSet.find(nicknameStr) != nicknameStringSet.end())
      return;

    nicknameStringSet.insert(nicknameStr);
    SaveToFile();

    // Уведомляем слушателей
    for (TListeners::iterator it_listener = listeners.begin(); it_listener != listeners.end(); ++it_listener)
    {
      if (const TListenerPtr listener = (*it_listener))
      {
        listener->OnUserAddedToIgnoreList(nickname);
      }
    }
  }

  void IgnoreListStorage::RemoveUser(const TUserNickname& nickname)
  {
    if (nickname.empty())
      return;

    nstl::string nicknameStr = NStr::ToMBCS(nickname);
    IgnoreList::TUserNicknameStringSet::iterator it = nicknameStringSet.find(nicknameStr);

    if (it == nicknameStringSet.end())
      return;

    nicknameStringSet.erase(it);
    SaveToFile();

    // Уведомляем слушателей
    for (TListeners::iterator it_listener = listeners.begin(); it_listener != listeners.end(); ++it_listener)
    {
      if (const TListenerPtr listener = (*it_listener))
      {
        listener->OnUserRemovedFromIgnoreList(nickname);
      }
    }
  }



  bool IgnoreListStorage::ContainsNickname(const TUserNickname& nickname) const
  {
    nstl::string nicknameStr = NStr::ToMBCS(nickname);
    MessageTrace("ContainsNickname: Checking nickname: '%s' (len=%d)", nicknameStr.c_str(), nicknameStr.length());
    MessageTrace("ContainsNickname: Total ignored nicknames: %d", nicknameStringSet.size());
    
    int index = 0;
    for (IgnoreList::TUserNicknameStringSet::const_iterator it = nicknameStringSet.begin(); it != nicknameStringSet.end(); ++it, ++index)
    {
      MessageTrace("ContainsNickname: [%d] '%s' (len=%d)", index, it->c_str(), it->length());
      if (nicknameStr == *it)
      {
        MessageTrace("ContainsNickname: EXACT MATCH found at index %d!", index);
      }
    }
    
    bool result = (nicknameStringSet.find(nicknameStr) != nicknameStringSet.end());
    MessageTrace("ContainsNickname: Result: %s", result ? "IGNORED" : "NOT_IGNORED");
    
    return result;
  }

  void IgnoreListStorage::LoadFromFile()
  {
    const IgnoreListReader reader(ownerUserId);
    reader(users, nicknames);
    
    // Заполняем nicknameStringSet из загруженных nicknames
    nicknameStringSet.clear();
    MessageTrace("LoadFromFile: Processing %d nicknames from file", nicknames.size());
    for (IgnoreList::TUserNicknameMap::const_iterator it = nicknames.begin(); it != nicknames.end(); ++it)
    {
      if (!it->second.empty())
      {
        nstl::string nicknameStr = NStr::ToMBCS(it->second);
        MessageTrace("LoadFromFile: Converting nickname: wstring='%s' -> string='%s' (len=%d)", 
                     NStr::ToMBCS(it->second).c_str(), nicknameStr.c_str(), nicknameStr.length());
        nicknameStringSet.insert(nicknameStr);
      }
    }
    
    // Если это новый формат (V3), users уже очищен в ReadNicknameOnlyFormat
    // Очищаем nicknames, оставляем только nicknameSet
    if (users.empty() && !nicknames.empty())
    {
      // Это признак нового формата - нет auid, только никнеймы
      nicknames.clear();
    }
  }

  void IgnoreListStorage::SaveToFile() const
  {
    const IgnoreListWriter writer(ownerUserId, nicknameStringSet);
    writer(users, nicknames);
  }

}

NI_DEFINE_REFCOUNT(NGameX::IgnoreListStorage);
NI_DEFINE_REFCOUNT(NGameX::IIgnoreListListener);