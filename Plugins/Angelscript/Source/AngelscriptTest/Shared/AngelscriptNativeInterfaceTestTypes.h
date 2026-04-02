#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "AngelscriptNativeInterfaceTestTypes.generated.h"

UINTERFACE(BlueprintType)
class ANGELSCRIPTTEST_API UAngelscriptNativeParentInterface : public UInterface
{
	GENERATED_BODY()
};

class ANGELSCRIPTTEST_API IAngelscriptNativeParentInterface
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent)
	int32 GetNativeValue() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent)
	void SetNativeMarker(FName Marker);
};

UINTERFACE(BlueprintType)
class ANGELSCRIPTTEST_API UAngelscriptNativeChildInterface : public UAngelscriptNativeParentInterface
{
	GENERATED_BODY()
};

class ANGELSCRIPTTEST_API IAngelscriptNativeChildInterface : public IAngelscriptNativeParentInterface
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent)
	int32 GetChildValue() const;
};
