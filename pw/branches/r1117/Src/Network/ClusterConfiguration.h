#pragma once

namespace Network
{

const string & GetCoordinatorAddress();
const string & GetLoginServerAddress();
void SetLoginServerAddress( const string & addr );
int GetFirstServerPort();

string const & GetFrontendIPAddr();
string const & GetBackendIPAddr();

} //namespace Network
