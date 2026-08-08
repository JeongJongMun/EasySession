// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionAddress.h"

#include "Engine/EngineBaseTypes.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	/**
	 * Index of the colon that separates the port, or INDEX_NONE when the string
	 * has no port we can point at with certainty.
	 */
	int32 FindPortSeparator(const FString& Address)
	{
		int32 BracketIndex = INDEX_NONE;
		if (Address.FindLastChar(TEXT(']'), BracketIndex))
		{
			// "[ipv6]:port" - the address is bracketed precisely so the port colon
			// can be told apart from the ones inside the address.
			return Address.Find(TEXT(":"), ESearchCase::CaseSensitive, ESearchDir::FromStart, BracketIndex);
		}

		int32 FirstColon = INDEX_NONE;
		int32 LastColon = INDEX_NONE;
		const bool bHasFirst = Address.FindChar(TEXT(':'), FirstColon);
		const bool bHasLast = Address.FindLastChar(TEXT(':'), LastColon);

		// One colon means "host:port". Several without brackets means a bare IPv6
		// address, where the trailing group is part of the address, not a port.
		return (bHasFirst && bHasLast && FirstColon == LastColon) ? FirstColon : INDEX_NONE;
	}
}

bool EasySessionAddress::HasZeroPort(const FString& ConnectString)
{
	const int32 ColonIndex = FindPortSeparator(ConnectString);
	if (ColonIndex == INDEX_NONE)
	{
		return false;
	}

	const FString PortText = ConnectString.Mid(ColonIndex + 1);
	if (PortText.IsEmpty() || !PortText.IsNumeric())
	{
		return false;
	}

	return FCString::Atoi(*PortText) == 0;
}

bool EasySessionAddress::HasListenOption(const FString& TravelURL)
{
	const FURL URL(nullptr, *TravelURL, TRAVEL_Absolute);
	return URL.HasOption(TEXT("listen"));
}

FString EasySessionAddress::ParseTravelOption(const FString& RequestURL, const TCHAR* Key)
{
	// ParseOption only reads from the first '?' onwards, and what arrives at
	// PreLogin has the map path in front (e.g. "/Game/Maps/Menu?Pw=x").
	const int32 OptionsStart = RequestURL.Find(TEXT("?"), ESearchCase::CaseSensitive);
	if (OptionsStart == INDEX_NONE)
	{
		return FString();
	}

	return UGameplayStatics::ParseOption(RequestURL.Mid(OptionsStart), Key).TrimStartAndEnd();
}

FString EasySessionAddress::EncodeTravelOptionValue(const FString& Value)
{
	// '%' first: it is the escape character, so escaping it after the others would
	// also mangle the escapes they just wrote.
	return Value
		.Replace(TEXT("%"), TEXT("%25"), ESearchCase::CaseSensitive)
		.Replace(TEXT("?"), TEXT("%3F"), ESearchCase::CaseSensitive)
		.Replace(TEXT("#"), TEXT("%23"), ESearchCase::CaseSensitive);
}

FString EasySessionAddress::DecodeTravelOptionValue(const FString& Value)
{
	// '%' last, mirroring the order above: a value that really contained "%3F"
	// was written as "%253F", and unescaping '%' first would turn it into a '?'.
	return Value
		.Replace(TEXT("%3F"), TEXT("?"), ESearchCase::CaseSensitive)
		.Replace(TEXT("%23"), TEXT("#"), ESearchCase::CaseSensitive)
		.Replace(TEXT("%25"), TEXT("%"), ESearchCase::CaseSensitive);
}
