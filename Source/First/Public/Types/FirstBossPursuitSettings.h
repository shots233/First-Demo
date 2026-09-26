#pragma once

#include "CoreMinimal.h"
#include "Types/FirstCombatTypes.h"
#include "FirstBossPursuitSettings.generated.h"

class UAnimSequence;
class UAnimMontage;

// BP_Boss 中集中配置行为树追击技能；普通攻击和三连击不再派生。
USTRUCT(BlueprintType)
struct FFirstBossPursuitSettings
{
	GENERATED_BODY()

	// 总开关：关闭后继续普通跑步追击。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pursuit")
	bool bEnabled = true;

	// 仅保留旧资产序列化兼容；触发策略已移至 BT 的 Boss Can Pursue 装饰器。
	UPROPERTY(meta=(DeprecatedProperty, DeprecationMessage="Configure Min Trigger Range on the Boss Can Pursue BT decorator."))
	float MinTriggerRange = 500.f;

	// 两次前闪追击之间的最短间隔；冷却未结束时不会再次触发。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pursuit", meta=(ClampMin="0", Units="s"))
	float Cooldown = 4.f;

	// 仅检查前闪预计落点能否接刀；不再同时限制后续刺击吸附。
	// 保留原字段名和数值以兼容现有蓝图。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pursuit", meta=(ClampMin="1", Units="cm", DisplayName="Predicted Landing Attack Range"))
	float DirectAttackRange = 250.f;

	// 刺击起手时，双方中心的水平距离在此范围内才允许吸附。0 关闭刺击吸附。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pursuit|Attack Warping", meta=(ClampMin="0", Units="cm", DisplayName="Attack Warp Range"))
	float AttackWarpRange = 450.f;

	// 从刺击起手位置到吸附目标点的最大水平位移；用于单独调节刺击推进力度。
	// 这是吸附终点的位移上限，不是追加在原动画位移上的距离；0 保留原动画位移。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pursuit|Attack Warping", meta=(ClampMin="0", Units="cm", DisplayName="Attack Warp Max Travel Distance"))
	float AttackWarpMaxTravelDistance = 300.f;

	// 刺击吸附终点与玩家胶囊表面保留的距离；不影响前闪的 Stop Surface Gap。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pursuit|Attack Warping", meta=(ClampMin="0", Units="cm", DisplayName="Attack Warp Surface Gap"))
	float AttackWarpSurfaceGap = 15.f;

	// 补偿刺击动画自身的偏向；负值向左、正值向右。与目标转角合计后仍受 MaxAttackFacingCorrection 限制。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pursuit|Attack Aiming", meta=(ClampMin="-45", ClampMax="45", Units="deg", DisplayName="Attack Aim Yaw Offset"))
	float AttackAimYawOffset = 0.f;

	// 吸附终点的横向偏移：以刺击起手时 BOSS 指向玩家的水平连线为基准，负值向左、正值向右。
	// 只在刺击吸附有效时应用；前进与横移的合计位移仍受 AttackWarpMaxTravelDistance 限制。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pursuit|Attack Aiming", meta=(ClampMin="-100", ClampMax="100", Units="cm", DisplayName="Attack Warp Lateral Offset"))
	float AttackWarpLateralOffset = 0.f;

	// 仅保留旧资产序列化兼容，不参与技能执行判断。
	UPROPERTY(meta=(DeprecatedProperty, DeprecationMessage="Configure Max Trigger Range on the Boss Can Pursue BT decorator."))
	float MaxTriggerRange = 650.f;

	// 前闪单次位移的最大距离；终点会根据此值与到玩家的距离取较小者。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pursuit", meta=(ClampMin="1", Units="cm"))
	float MaxDashDistance = 450.f;

	// 前闪终点在双方胶囊表面外保留的空间。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pursuit", meta=(ClampMin="0", Units="cm"))
	float StopSurfaceGap = 80.f;

	// 允许进入前闪的起手指向误差：玩家偏离 BOSS 正面超过该角度则不触发。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pursuit", meta=(ClampMin="0", ClampMax="180", Units="deg"))
	float MaxStartFacingAngle = 100.f;

	// 前闪之后最多修正此角度，再固定追击方向；玩家超出角度时仍会挥刀。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pursuit", meta=(ClampMin="0", ClampMax="180", Units="deg"))
	float MaxAttackFacingCorrection = 35.f;

	// 允许触发追击的双方高度差上限；玩家跳起/站上高台超过该值则不前闪。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pursuit", meta=(ClampMin="0", Units="cm"))
	float MaxTargetHeightDifference = 100.f;

	// 优先使用已制作的前闪蒙太奇，保留资源自身的 Slot、混入混出和通知。
	// 使用 BOSS 骨架、启用根运动的序列，Section 按时间顺序播放一次并自动混出。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pursuit|Animation")
	TObjectPtr<UAnimMontage> ForwardDodgeMontage;

	// 兼容原配置：没有指定前闪蒙太奇时，从此序列生成临时蒙太奇。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pursuit|Animation", meta=(EditCondition="ForwardDodgeMontage == nullptr"))
	TObjectPtr<UAnimSequence> ForwardDodgeAnimation;

	// 仅用于序列模式；None 时使用 DefaultSlot。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pursuit|Animation", meta=(EditCondition="ForwardDodgeMontage == nullptr"))
	FName DodgeSlotName = NAME_None;

	// 前闪动画（蒙太奇或序列模式）的播放速率；越大突进越快、窗口越短。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pursuit|Animation", meta=(ClampMin="0.1"))
	float DodgePlayRate = 1.5f;

	// 留空使用 BP_Boss 的 NormalAttackMontage；以后可换成新的追击攻击。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pursuit|Animation")
	TObjectPtr<UAnimMontage> FollowupAttackMontage;

	// 勾选时追击攻击复用普通攻击的伤害；取消勾选才使用下方自定义 AttackDamage。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pursuit|Damage")
	bool bUseNormalAttackDamage = true;

	// 追击攻击的独立基础伤害（仅在未勾选"复用普通攻击伤害"时生效）。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pursuit|Damage", meta=(EditCondition="!bUseNormalAttackDamage", ClampMin="0"))
	float AttackDamage = 20.f;

	// 勾选时追击攻击复用普通攻击的格挡/弹反规则；取消勾选才使用下方独立防御数据。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pursuit|Damage")
	bool bUseNormalAttackDefense = true;

	// 追击攻击独立的可格挡/可弹反/削韧等防御数据（仅在未勾选"复用普通攻击防御"时生效）。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pursuit|Damage", meta=(EditCondition="!bUseNormalAttackDefense"))
	FFirstMeleeDefenseData AttackDefenseData;
};
