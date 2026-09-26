// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "FirstGE_StaminaRegenPause.generated.h"

/**
 * 格挡受击后的精力恢复暂停标记（授予 DK.Status.StaminaRegenPaused，压制两条恢复 GE）。
 */
UCLASS()
class FIRST_API UFirstGE_StaminaRegenPause : public UGameplayEffect
{
	GENERATED_BODY()
public:
	UFirstGE_StaminaRegenPause();

	// 蓝图子类的默认值不走本构造函数，序列化完成后在这里把可调参数同步进 Duration。
	virtual void PostInitProperties() override;

protected:
	// 恢复被暂停的时长，单位秒。每次格挡受击会刷新（StackLimit=1 + RefreshOnSuccessfulApplication）。
	UPROPERTY(EditDefaultsOnly, Category="Stamina|Pause", meta=(ClampMin="0.0"))
	float PauseDurationSeconds = 0.75f;
};
