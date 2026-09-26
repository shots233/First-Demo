// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "TimerManager.h"
#include "FirstPlayerHUDWidget.generated.h"

class ADKCharacter;
class UBorder;
class UDKUIComponent;
class UFirstAbilitySystemComponent;
class UProgressBar;
/**
 * 
 */
UCLASS()
class FIRST_API UFirstPlayerHUDWidget : public UUserWidget
{
	GENERATED_BODY()
protected:
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

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

	// 与 StaminaBar 同处一个 Overlay，作为上层描边，不作为精力条的父控件。
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UBorder> StaminaExhaustedBorder;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="UI|Stamina")
	FLinearColor StaminaExhaustedBorderColor = FLinearColor(1.f, 0.08f, 0.08f, 1.f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="UI|Stamina", meta=(ClampMin="0.0"))
	float StaminaExhaustedBorderThickness = 2.f;
	
	// 出生时 HUD 创建可能早于玩家被 Possess，取不到玩家就下一帧重试。
	void RetryBindPlayer();

private:
	void UnbindFromPlayer();
	void HandleDodgeExhaustedChanged(const FGameplayTag Tag, int32 NewCount);
	void ApplyStaminaExhaustedBorderStyle();
	void SetStaminaExhaustedBorderVisible(bool bVisible);

	TWeakObjectPtr<UDKUIComponent> BoundDKUI;
	TWeakObjectPtr<UFirstAbilitySystemComponent> BoundASC;
	FDelegateHandle DodgeExhaustedChangedHandle;
	FTimerHandle RetryBindPlayerTimerHandle;
	bool bIsConstructed = false;
};
