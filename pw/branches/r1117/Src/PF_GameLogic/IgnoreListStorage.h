#pragma once

namespace NGameX
{

namespace IgnoreList
{
  typedef unsigned long long TUserId;
  typedef nstl::wstring TUserNickname;

  typedef nstl::set<TUserId> TUserSet;
  typedef nstl::map<TUserId, TUserNickname> TUserNicknameMap;
  typedef nstl::set<TUserNickname> TUserNicknameSet;
  typedef nstl::set<nstl::string> TUserNicknameStringSet;
}

using IgnoreList::TUserId;
using IgnoreList::TUserNickname;

_interface IIgnoreListListener : public IBaseInterfaceST
{
  NI_DECLARE_CLASS_1( IIgnoreListListener, IBaseInterfaceST );

  virtual void OnUserAddedToIgnoreList(const TUserNickname& nickname) = 0;
  virtual void OnUserRemovedFromIgnoreList(const TUserNickname& nickname) = 0;
};


class IgnoreListStorage: public BaseObjectST
{
  NI_DECLARE_REFCOUNT_CLASS_1( IgnoreListStorage, BaseObjectST );
public:
  explicit IgnoreListStorage(const TUserId _auid);

  void AddListener(IIgnoreListListener* const listener);

  void AddUser(const TUserNickname& nickname);
  void RemoveUser(const TUserNickname& nickname);

  bool ContainsNickname(const TUserNickname& nickname) const;

  bool ContainsUser(const TUserId auid) const
  {
    return (users.find(auid) != users.end());
  }

  void LoadFromFile();
  void SaveToFile() const;

  const IgnoreList::TUserSet& GetIgnoredUsers() const
  {
    return users;
  }

  const IgnoreList::TUserNicknameStringSet& GetIgnoredNicknames() const
  {
    return nicknameStringSet;
  }
private:
  typedef Weak<IIgnoreListListener> TListenerWeakPtr;
  typedef Strong<IIgnoreListListener> TListenerPtr;
  typedef list<TListenerWeakPtr> TListeners;

  IgnoreListStorage();
  IgnoreListStorage(const IgnoreListStorage&);
  IgnoreListStorage& operator=(const IgnoreListStorage&);

  const TUserId ownerUserId;

  TListeners listeners;

  IgnoreList::TUserSet users;
  IgnoreList::TUserNicknameMap nicknames;
  IgnoreList::TUserNicknameStringSet nicknameStringSet;
};

}
