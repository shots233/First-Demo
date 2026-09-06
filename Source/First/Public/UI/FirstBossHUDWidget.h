// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FirstBossHUDWidget.generated.h"

class ABossCharacter;
class UProgressBar;
class UTextBlock;
/**
 * 
 */
UCLASS()
class FIRST_API UFirstBossHUDWidget : public UUserWidget
{
	GENERATED_BODY()
protected:
	virtual void NativeConstruct() override;

	// 找到 BOSS 并绑定委托、读取当前值。
	void BindToBoss(ABossCharacter* InBoss);

	UFUNCTION()
	void HandleHealthChanged(float NewPercent);

	UFUNCTION()
	void HandlePoiseChanged(float NewPercent);

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UTextBlock> BossNameText;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UProgressBar> BossHealthBar;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UProgressBar> BossPoiseBar;
};
