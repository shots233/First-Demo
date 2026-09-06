#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Types/FirstCombatTypes.h"
#include "DKDefenseComponent.generated.h"

class UCameraShakeBase;
class UAbilitySystemComponent;
class UParticleSystem;
class USoundBase;

UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class FIRST_API UDKDefenseComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDKDefenseComponent();

	// BOSS 攻击 GA 在真正应用生命伤害前调用一次。
	EFirstDefenseResult ResolveIncomingMeleeAttack(
		AActor* Attacker,
		const FFirstMeleeDefenseData& AttackData);

	FORCEINLINE float GetMinimumStaminaToGuard() const
	{
		return MinimumStaminaToGuard;
	}

	FORCEINLINE float GetParryWindowDuration() const
	{
		return ParryWindowDuration;
	}

	FORCEINLINE float GetGuardMoveSpeed() const
	{
		return GuardMoveSpeed;
	}

	FORCEINLINE float GetGuardBreakDuration() const
	{
		return GuardBreakDuration;
	}

private:
	bool IsAttackerInsideGuardArc(const AActor* Attacker) const;
	void ApplyStaminaDelta(UAbilitySystemComponent* ASC, AActor* Attacker, float Delta) const;
	void ApplyStaminaRegenPause(UAbilitySystemComponent* ASC, AActor* Attacker) const;

	// 这里保存半角。60° 表示总格挡范围 120°。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Defense|Guard",
		meta=(AllowPrivateAccess="true", ClampMin="0.0", ClampMax="180.0"))
	float GuardHalfAngleDegrees = 60.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Defense|Guard",
		meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float MinimumStaminaToGuard = 20.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Defense|Parry",
		meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float ParryWindowDuration = 0.15f;

	// 格挡蒙太奇通过上半身插槽播放，保留下半身移动并限制移动速度。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Defense|Guard",
		meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float GuardMoveSpeed = 125.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Defense|Guard Break",
		meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float GuardBreakDuration = 1.3f;

	// —— 防御结果音 ——
	// 在 BP_DKCharacter 的 DKDefenseComponent 默认值里配置；留空则该分支静默。
	// 全部从玩家角色位置发出 3D 空间化音效。
	// 注意：BOSS 命中玩家的音效/血花已按"攻击者组织"移到 BP_Boss（Boss|Feedback），
	// 由 BOSS 攻击 GA 判定 Damaged 后播放；本组件只保留玩家自己防御的表现。

	// 普通格挡成功（命中但被挡住）。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Defense|SFX",
		meta=(AllowPrivateAccess="true"))
	TObjectPtr<USoundBase> GuardHitSound;

	// 弹反成功。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Defense|SFX",
		meta=(AllowPrivateAccess="true"))
	TObjectPtr<USoundBase> ParrySuccessSound;

	// 格挡被破防。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Defense|SFX",
		meta=(AllowPrivateAccess="true"))
	TObjectPtr<USoundBase> GuardBrokenSound;

	// 在玩家角色位置播放一次 3D 防御结果音；Sound 为空时静默。
	void PlayDefenseSFX(USoundBase* Sound) const;

	// —— 防御结果特效 ——
	// 与音效同源判定，唯一区别是显示层：在玩家躯干位置生成一次性级联粒子（火花）。
	// 在 BP_DKCharacter 的 DKDefenseComponent 默认值里配置；留空则该分支无特效。
	// 注意：受击血花已移到 BP_Boss（Boss|Feedback），本组件只保留格挡/弹反/破防火花。

	// 普通格挡成功（命中但被挡住）：金属火花。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Defense|VFX",
		meta=(AllowPrivateAccess="true"))
	TObjectPtr<UParticleSystem> GuardHitVFX;

	// 弹反成功：更明亮的火花。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Defense|VFX",
		meta=(AllowPrivateAccess="true"))
	TObjectPtr<UParticleSystem> ParrySuccessVFX;

	// 格挡被破防：重击炸裂。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Defense|VFX",
		meta=(AllowPrivateAccess="true"))
	TObjectPtr<UParticleSystem> GuardBrokenVFX;

	// 在玩家躯干位置生成一次性防御结果特效，方向向来击侧（Attacker → Defender）；为空时静默。
	void PlayDefenseVFX(UParticleSystem* VFX, const AActor* Attacker) const;

	// —— 镜头震动 ——
	// 玩家视角的打击反馈：弹反/格挡成功瞬间让镜头抖一下。
	// 在内容浏览器创建"摄像机抖动"蓝图资产（父类 CameraShakeBase，图案用 Wave Oscillator），
	// 然后在这里拖入；留空则该分支不震动。

	// 弹反成功时的镜头震动。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Defense|Shake",
		meta=(AllowPrivateAccess="true"))
	TSubclassOf<UCameraShakeBase> ParryShakeClass;

	// 弹反震动的强度倍率（1.0 = 资产自带强度）。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Defense|Shake",
		meta=(AllowPrivateAccess="true", ClampMin="0.0", UIMin="0.0"))
	float ParryShakeScale = 1.2f;

	// 普通格挡成功时的镜头震动。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Defense|Shake",
		meta=(AllowPrivateAccess="true"))
	TSubclassOf<UCameraShakeBase> GuardHitShakeClass;

	// 格挡震动的强度倍率（比弹反轻很多）。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Defense|Shake",
		meta=(AllowPrivateAccess="true", ClampMin="0.0", UIMin="0.0"))
	float GuardHitShakeScale = 0.6f;

	// 在玩家相机上播放一次震动；ShakeClass 为空时静默。
	void PlayDefenseShake(TSubclassOf<UCameraShakeBase> ShakeClass, float Scale) const;
};
