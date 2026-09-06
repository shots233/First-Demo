// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_PlaySFX.generated.h"

class USoundBase;
class USoundAttenuation;

/**
 * 通用 SFX 播放通知。
 *
 * 放在 Montage / AnimSequence 的某一帧：播放到该帧时从 Sounds 数组随机取一个播放。
 * 只负责"播一下声音"，不参与任何玩法判定（命中、格挡等结果音请走事件回调）。
 * 挥砍（Woosh）、拔剑、脚步等纯反馈音都在这里放：
 * 多个 01~07 变体随机挑选，配合 Pitch 随机可有效消除连击的重复感。
 *
 * 与 ANS_WeaponTelegraph 的分工：
 * - 本通知 = 一次性瞬间音效，无状态、无防重；
 * - 预警白光+提示音 = 需要状态锁与防重，仍走 SetWeaponTelegraphEnabled。
 */
UCLASS(meta = (DisplayName = "Play SFX"))
class FIRST_API UAnimNotify_PlaySFX : public UAnimNotify
{
	GENERATED_BODY()
public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

	// 候选音效列表：每次触发随机选一个。
	// 至少放 3~7 个变体（如需切片 01~07），只有 1 个时直接播放。
	UPROPERTY(EditAnywhere, Category="SFX")
	TArray<TObjectPtr<USoundBase>> Sounds;

	// 整体音量缩放；生效值 = 音效自身音量 × 本值。
	UPROPERTY(EditAnywhere, Category="SFX", meta=(ClampMin="0.0", UIMin="0.0"))
	float VolumeMultiplier = 1.f;

	// 每次触发在 [Min, Max] 内随机取一个 Pitch 倍率（1.0 = 原始音高）。
	UPROPERTY(EditAnywhere, Category="SFX", meta=(ClampMin="0.1", UIMin="0.1"))
	FVector2D PitchRange = FVector2D(0.95f, 1.05f);

	// 可选：3D 空间化使用的衰减资产。留空则使用 SoundBase 自身配置。
	UPROPERTY(EditAnywhere, Category="SFX")
	TObjectPtr<USoundAttenuation> AttenuationOverride;

	// 可选：从骨架指定 Socket 位置发声（如武器挂点）。留空 = 角色 Mesh 位置。
	UPROPERTY(EditAnywhere, Category="SFX", meta=(DisplayName="Socket Name (Optional)"))
	FName AttachSocketName;
};
