// WorkerBrokerage - Manage worker discovery
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "WorkerBrokerage.h"

// FBuild
#include "Tools/FBuild/FBuildCore/Protocol/Protocol.h"
#include "Tools/FBuild/FBuildCore/WorkerPool/WorkerConnectionPool.h"

// Core
#include "Core/Env/Env.h"
#include "Core/Network/Network.h"
#include "Core/Profile/Profile.h"
#include "Core/Strings/AStackString.h"
#include "Core/Tracing/Tracing.h"

#if defined( __APPLE__ )

#include <sys/socket.h>
#include <ifaddrs.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static bool ConvertHostNameToLocalIP4( AString& hostName )
{
    bool result = false;

    struct ifaddrs * allIfAddrs;
    if ( getifaddrs( &allIfAddrs ) == 0 )
    {
        struct ifaddrs * addr = allIfAddrs;
        char ipString[48] = { 0 };
        while ( addr )
        {
            if ( addr->ifa_addr )
            {
                if ( addr->ifa_addr->sa_family == AF_INET && strcmp( addr->ifa_name, "en0" ) == 0 )
                {
                    struct sockaddr_in * sockaddr = ( struct sockaddr_in * ) addr->ifa_addr;
                    inet_ntop( AF_INET, &sockaddr->sin_addr, ipString, sizeof( ipString ) );
                    hostName = ipString;
                    result = true;
                    break;
                }
            }
            addr = addr->ifa_next;
        }

        freeifaddrs( allIfAddrs );
    }

    return result;
}

#endif // __APPLE__

// CONSTRUCTOR
//------------------------------------------------------------------------------
WorkerBrokerage::WorkerBrokerage()
    : m_BrokerageInitialized( false )
    , m_ConnectionPool( nullptr )
    , m_Connection( nullptr )
    , m_WorkerListUpdateReady( false )
{
}

// InitBrokerage
//------------------------------------------------------------------------------
void WorkerBrokerage::InitBrokerage()
{
    PROFILE_FUNCTION;

    if ( m_BrokerageInitialized )
    {
        return;
    }

#if defined( __APPLE__ )
    ConvertHostNameToLocalIP4(m_HostName);
#endif

    if ( m_CoordinatorAddress.IsEmpty() == true )
    {
        AStackString<> coordinator;
        if ( Env::GetEnvVariable( "FASTBUILD_COORDINATOR", coordinator ) )
        {
            m_CoordinatorAddress = coordinator;
        }
    }

    if ( m_CoordinatorAddress.IsEmpty() == true )
    {
        OUTPUT( "Using brokerage folder\n" );

        // brokerage path includes version to reduce unnecssary comms attempts
        const uint32_t protocolVersion = Protocol::PROTOCOL_VERSION_MAJOR;

        // root folder
        AStackString<> brokeragePath;
        if ( Env::GetEnvVariable( "FASTBUILD_BROKERAGE_PATH", brokeragePath ) )
        {
            // FASTBUILD_BROKERAGE_PATH can contain multiple paths separated by semi-colon. The worker will register itself into the first path only but
            // the additional paths are paths to additional broker roots allowed for finding remote workers (in order of priority)
            const char * start = brokeragePath.Get();
            const char * end = brokeragePath.GetEnd();
            AStackString<> pathSeparator( ";" );
            while ( true )
            {
                AStackString<> root;
                AStackString<> brokerageRoot;

                const char * separator = brokeragePath.Find( pathSeparator, start, end );
                if ( separator != nullptr )
                {
                    root.Append( start, (size_t)( separator - start ) );
                }
                else
                {
                    root.Append( start, (size_t)( end - start ) );
                }
                root.TrimStart( ' ' );
                root.TrimEnd( ' ' );
                // <path>/<group>/<version>/
                #if defined( __WINDOWS__ )
                    brokerageRoot.Format( "%s\\main\\%u.windows\\", root.Get(), protocolVersion );
                #elif defined( __OSX__ )
                    brokerageRoot.Format( "%s/main/%u.osx/", root.Get(), protocolVersion );
                #else
                    brokerageRoot.Format( "%s/main/%u.linux/", root.Get(), protocolVersion );
                #endif

                m_BrokerageRoots.Append( brokerageRoot );
                if ( !m_BrokerageRootPaths.IsEmpty() )
                {
                    m_BrokerageRootPaths.Append( pathSeparator );
                }

                m_BrokerageRootPaths.Append( brokerageRoot );

                if ( separator != nullptr )
                {
                    start = separator + 1;
                }
                else
                {
                    break;
                }
            }
        }

    }
    else
    {
        OUTPUT( "Using coordinator\n" );
    }

    m_BrokerageInitialized = true;
}

// DESTRUCTOR
//------------------------------------------------------------------------------
WorkerBrokerage::~WorkerBrokerage() = default;


// ConnectToCoordinator
//------------------------------------------------------------------------------
bool WorkerBrokerage::ConnectToCoordinator()
{
    if ( m_CoordinatorAddress.IsEmpty() == false )
    {
        m_ConnectionPool = FNEW( WorkerConnectionPool );
        m_Connection = m_ConnectionPool->Connect( m_CoordinatorAddress, Protocol::COORDINATOR_PORT, 2000, this ); // 2000ms connection timeout
        if ( m_Connection == nullptr )
        {
            OUTPUT( "Failed to connect to the coordinator at %s\n", m_CoordinatorAddress.Get() );
            FDELETE m_ConnectionPool;
            m_ConnectionPool = nullptr;
            // m_CoordinatorAddress.Clear();
            return false;
        }

        OUTPUT( "Connected to the coordinator\n" );
        return true;
    }

    return false;
}

// DisconnectFromCoordinator
//------------------------------------------------------------------------------
void WorkerBrokerage::DisconnectFromCoordinator()
{
    if ( m_ConnectionPool )
    {
        FDELETE m_ConnectionPool;
        m_ConnectionPool = nullptr;
        m_Connection = nullptr;

        OUTPUT( "Disconnected from the coordinator\n" );
    }
}

//------------------------------------------------------------------------------
