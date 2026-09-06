// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PawnUIComponent.generated.h"

// 属性变化时广播的百分比（0~1）。
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPercentChangedDelegate, float, NewPercent);

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class FIRST_API UPawnUIComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	UPROPERTY(BlueprintAssignable, Category="UI")
	FOnPercentChangedDelegate OnCurrentHealthChanged;
	
	UPROPERTY(BlueprintAssignable, Category="UI")
	FOnPercentChangedDelegate OnCurrentStaminaChanged;
	
	UPROPERTY(BlueprintAssignable, Category="UI")
	FOnPercentChangedDelegate OnCurrentPoiseChanged;
		
};
