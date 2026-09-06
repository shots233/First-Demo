// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FirstPlayerHUDWidget.generated.h"

class ADKCharacter;
class UProgressBar;
/**
 * 
 */
UCLASS()
class FIRST_API UFirstPlayerHUDWidget : public UUserWidget
{
	GENERATED_BODY()
protected:
	virtual void NativeConstruct() override;

	// 找到玩家并绑定委托、读取当前值。
	void BindToPlayer(ADKCharacter* InPlayer);

	UFUNCTION()
	void HandleHealthChanged(float NewPercent);

	UFUNCTION()
	void HandleStaminaChanged(float NewPercent);

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UProgressBar> HealthBar;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UProgressBar> StaminaBar;
	
	// 出生时 HUD 创建可能早于玩家被 Possess，取不到玩家就下一帧重试。
	void RetryBindPlayer();
};
