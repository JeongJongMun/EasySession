// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionBeaconPort.h"

#include "EasySession.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "OnlineBeaconHost.h"
#include "OnlineBeaconHostObject.h"

FEasySessionBeaconPort::~FEasySessionBeaconPort()
{
	ReleaseListener();
}

bool FEasySessionBeaconPort::Register(AOnlineBeaconHostObject& HostObject)
{
	UWorld* World = HostObject.GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	// A travel destroyed the previous listener together with everything registered on it.
	if (!Listener.IsValid() || Listener->GetWorld() != World)
	{
		Listener = FindOrSpawnListener(*World);
		RegisteredCount = 0;
	}

	AOnlineBeaconHost* Host = Listener.Get();
	if (Host == nullptr)
	{
		return false;
	}

	Host->RegisterHost(&HostObject);
	++RegisteredCount;

	// The project's own listener keeps the pause state the project chose.
	if (bOwnsListener)
	{
		Host->PauseBeaconRequests(false);
	}

	UE_LOG(LogEasySession, Log, TEXT("Registered the '%s' beacon on %s listener (port %d)."),
		*HostObject.GetBeaconType(), bOwnsListener ? TEXT("the plugin's") : TEXT("the project's"), Host->GetListenPort());
	return true;
}

void FEasySessionBeaconPort::Unregister(AOnlineBeaconHostObject& HostObject)
{
	HostObject.Unregister();

	RegisteredCount = FMath::Max(0, RegisteredCount - 1);
	if (RegisteredCount == 0)
	{
		ReleaseListener();
	}
}

int32 FEasySessionBeaconPort::GetListenPort() const
{
	// GetListenPort is not const on the engine class, so the pointer cannot be either.
	AOnlineBeaconHost* Host = Listener.Get();
	return Host != nullptr ? Host->GetListenPort() : 0;
}

AOnlineBeaconHost* FEasySessionBeaconPort::GetListener() const
{
	return Listener.Get();
}

AOnlineBeaconHost* FEasySessionBeaconPort::FindOrSpawnListener(UWorld& World)
{
	// A listener is one per process, so an existing one is reused instead of binding a second port that no session advertises.
	for (TActorIterator<AOnlineBeaconHost> It(&World); It; ++It)
	{
		bOwnsListener = false;
		return *It;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	AOnlineBeaconHost* Host = World.SpawnActor<AOnlineBeaconHost>(SpawnParams);
	if (Host == nullptr)
	{
		return nullptr;
	}

	// ListenPort is left as the class default so a project can move the beacon from DefaultEngine.ini.
	if (!Host->InitHost())
	{
		UE_LOG(LogEasySession, Error, TEXT("Could not start the beacon listener."));
		UE_LOG(LogEasySession, Error,
			TEXT("Likely causes: no BeaconNetDriver definition (clearing NetDriverDefinitions removes the engine's), or a plain ServerTravel kept the previous beacon's port. Change maps with Server Travel Easy Session."));
		Host->DestroyBeacon();
		return nullptr;
	}

	bOwnsListener = true;
	return Host;
}

void FEasySessionBeaconPort::ReleaseListener()
{
	// The project's own listener stays up for the project.
	// Only a listener this object spawned is destroyed.
	if (AOnlineBeaconHost* Host = Listener.Get(); Host != nullptr && bOwnsListener)
	{
		Host->DestroyBeacon();
	}

	Listener.Reset();
	bOwnsListener = false;
	RegisteredCount = 0;
}
