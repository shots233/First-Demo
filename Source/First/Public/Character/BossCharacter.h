// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Character/EnemyCharacter.h"
#include "Types/FirstCombatTypes.h"
#include "TimerManager.h"
#include "BossCharacter.generated.h"

class UEnemyUIComponent;
struct FOnAttributeChangeData;
class UBossCombatComponent;
class ABossWeapon;
class USceneComponent;
class UParticleSystem;
class USoundBase;

UENUM(BlueprintType)
enum class EBossRotationMode : uint8
{
	OrientToMovement UMETA(DisplayName="朝移动方向"),
	FaceTarget       UMETA(DisplayName="面向目标"),
	Frozen           UMETA(DisplayName="冻结自动旋转")
};
/**
 * 
 */
UCLASS()
class FIRST_API ABossCharacter : public AEnemyCharacter
{
	GENERATED_BODY()
	
public:
	ABossCharacter();
	// 顶部 BOSS 血条显示的名字（在 BP_Boss 类默认值里填）。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|UI", meta=(AllowPrivateAccess="true"))
	FText BossName;

	// IPawnUIInterface：返回 BOSS UI 组件。
	virtual UPawnUIComponent* GetPawnUIComponent() const override;

	FORCEINLINE UEnemyUIComponent* GetEnemyUIComponent() const { return EnemyUIComponent; }
	
	// —— 巡逻参数 ——

	// 巡逻圆心。默认用出生位置作为家点。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Patrol", meta=(AllowPrivateAccess="true"))
	FVector HomeLocation = FVector::ZeroVector;

	// true：出生时把当前出生位置写入 HomeLocation；
	// false：使用上面手动填写的 HomeLocation（例如做固定战场的 BOSS）。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Patrol", meta=(AllowPrivateAccess="true"))
	bool bUseSpawnLocationAsHome = true;

	// 巡逻半径：BOSS 只在 HomeLocation 周围这个半径内选取巡逻点。
	// 决策书建议值 800～1200，默认 1200。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Patrol", meta=(AllowPrivateAccess="true"))
	float PatrolRadius = 1200.f;

	// 周旋距离（决策书 D28）：对峙时侧移/后退的目标半径。
	// 首版与 UBTService_UpdateCombatDistance::StrafeRadius 统一为 500，
	// 避免边界附近行为频繁切换导致八方向动画抖动。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Patrol", meta=(AllowPrivateAccess="true"))
	float StrafeRadius = 500.f;
	
	// 前往巡逻点时的移动速度：步行。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Patrol", meta=(AllowPrivateAccess="true"))
	float PatrolMoveSpeed = 250.f;

	// 追击玩家时的移动速度：奔跑。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Patrol", meta=(AllowPrivateAccess="true"))
	float ChaseMoveSpeed = 500.f;

	// 统一设置当前移动速度（巡逻 250 / 追击 500 由行为树服务调用）。
	void SetMovementSpeed(float NewSpeed);
	
	// 受击/死亡蒙太奇，在 BP_Boss 类默认值里配置。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Anim", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UAnimMontage> HitReactMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Anim", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UAnimMontage> DeathMontage;

	FORCEINLINE UAnimMontage* GetHitReactMontage() const { return HitReactMontage; }
	FORCEINLINE UAnimMontage* GetDeathMontage() const { return DeathMontage; }
	
	// 武器生成类：在 BP_Boss 里填 BP_BossWeapon。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Weapon", meta=(AllowPrivateAccess="true"))
	TSubclassOf<ABossWeapon> BossWeaponClass;

	// 背部挂点 / 手部挂点（在 SK_DKF_Full 骨架上创建）。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Weapon", meta=(AllowPrivateAccess="true"))
	FName BackSocketName = TEXT("BossWeapon_Back");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Weapon", meta=(AllowPrivateAccess="true"))
	FName HandSocketName = TEXT("BossWeapon_Hand");

	// 招式蒙太奇（在 BP_Boss 里配置）。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Anim", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UAnimMontage> DrawSwordMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Anim", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UAnimMontage> NormalAttackMontage;

	// 三连击：单个完整三连蒙太奇（动画本身包含三段挥砍）。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Anim", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UAnimMontage> ThreeComboMontage;

	// 五连斩（霸体招式）：单个完整五连蒙太奇（五个 Section 顺序衔接）。
	// 挥砍段由 ANS_SuperArmorWindow 覆盖，第五段后摇放 ANS_HitReactWindow 惩罚窗口。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Anim", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UAnimMontage> FiveComboMontage;

	// 后撤蓄力斩：包含后撤、蓄力预警和前冲斩击的完整蒙太奇。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Anim", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UAnimMontage> RetreatChargedSlashMontage;

	FORCEINLINE UBossCombatComponent* GetBossCombatComponent() const { return BossCombatComponent; }
	FORCEINLINE UAnimMontage* GetDrawSwordMontage() const { return DrawSwordMontage; }
	FORCEINLINE UAnimMontage* GetNormalAttackMontage() const { return NormalAttackMontage; }
	FORCEINLINE UAnimMontage* GetThreeComboMontage() const { return ThreeComboMontage; }
	FORCEINLINE UAnimMontage* GetFiveComboMontage() const { return FiveComboMontage; }

	UFUNCTION(BlueprintPure, Category="Boss|Anim")
	FORCEINLINE UAnimMontage* GetRetreatChargedSlashMontage() const
	{
		return RetreatChargedSlashMontage;
	}

	FORCEINLINE float GetStrafeRadius() const { return StrafeRadius; }

	// —— 招式伤害（在 BP_Boss 类默认值里调，不用改代码）——

	// 普通攻击基础伤害。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Combat", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float NormalAttackDamage = 20.f;

	// 三连击每段基础伤害。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Combat", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float ThreeComboDamagePerHit = 22.f;

	// 五连斩每段基础伤害：首版略低于三连（五段总量更高）。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Combat", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float FiveComboDamagePerHit = 20.f;

	// 后撤蓄力斩的单次基础伤害。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Combat", meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float RetreatChargedSlashDamage = 35.f;

	FORCEINLINE float GetNormalAttackDamage() const { return NormalAttackDamage; }
	FORCEINLINE float GetThreeComboDamagePerHit() const { return ThreeComboDamagePerHit; }
	FORCEINLINE float GetFiveComboDamagePerHit() const { return FiveComboDamagePerHit; }

	UFUNCTION(BlueprintPure, Category="Boss|Combat")
	FORCEINLINE float GetRetreatChargedSlashDamage() const
	{
		return RetreatChargedSlashDamage;
	}

	// 普通攻击的防御白名单与资源伤害。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Combat|Defense",meta=(AllowPrivateAccess="true"))
	FFirstMeleeDefenseData NormalAttackDefenseData;

	// 三连三段首版共用同一描述，但每个碰撞窗口会独立解析一次。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Combat|Defense",meta=(AllowPrivateAccess="true"))
	FFirstMeleeDefenseData ThreeComboDefenseData;

	// 五连斩的防御白名单与资源伤害（五段共用同一描述，逐段独立解析）。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Combat|Defense",meta=(AllowPrivateAccess="true"))
	FFirstMeleeDefenseData FiveComboDefenseData;

	// 后撤蓄力斩的防御白名单与资源伤害。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Combat|Defense", meta=(AllowPrivateAccess="true"))
	FFirstMeleeDefenseData RetreatChargedSlashDefenseData;

	FORCEINLINE const FFirstMeleeDefenseData& GetNormalAttackDefenseData() const{return NormalAttackDefenseData;}

	FORCEINLINE const FFirstMeleeDefenseData& GetThreeComboDefenseData() const{return ThreeComboDefenseData;}

	FORCEINLINE const FFirstMeleeDefenseData& GetFiveComboDefenseData() const{return FiveComboDefenseData;}

	UFUNCTION(BlueprintPure, Category="Boss|Combat|Defense")
	FORCEINLINE FFirstMeleeDefenseData GetRetreatChargedSlashDefenseData() const
	{
		return RetreatChargedSlashDefenseData;
	}

	// —— 命中反馈（配在 BP_Boss 上）——
	// BOSS 攻击命中玩家（未防御成功）：音效与血花生成在玩家身上；
	// 由 BOSS 攻击 GA 在判定 Damaged 后调用，玩家防御侧不再重复配置。

	// BOSS 攻击命中玩家的音效（从玩家位置发出）。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Feedback",
		meta=(AllowPrivateAccess="true"))
	TObjectPtr<USoundBase> HitPlayerSound;

	// BOSS 攻击命中玩家的血花（生成在玩家躯干位置）。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Feedback",
		meta=(AllowPrivateAccess="true"))
	TObjectPtr<UParticleSystem> HitPlayerVFX;

	// BOSS 被玩家/敌人击中的血花（生成在 BOSS 躯干位置；由攻击方读取）。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Feedback",
		meta=(AllowPrivateAccess="true"))
	TObjectPtr<UParticleSystem> BloodVFX;

	UFUNCTION(BlueprintPure, Category="Boss|Feedback")
	FORCEINLINE USoundBase* GetHitPlayerSound() const { return HitPlayerSound; }

	UFUNCTION(BlueprintPure, Category="Boss|Feedback")
	FORCEINLINE UParticleSystem* GetHitPlayerVFX() const { return HitPlayerVFX; }

	UFUNCTION(BlueprintPure, Category="Boss|Feedback")
	FORCEINLINE UParticleSystem* GetBloodVFX() const { return BloodVFX; }

	// 检查完整后撤路径是否无阻挡，且预期落点属于当前 BOSS NavAgent 的可行走 NavMesh。
	UFUNCTION(BlueprintPure, Category="Boss|Movement")
	bool HasSafeRetreatSpace(
		float RetreatDistance,
		float NavProjectionTolerance = 50.f,
		float MaxLandingHeightDelta = 60.f) const;
	
	//弹反
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Combat|Poise",meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float ParryStaggerDuration = 1.f;

	// 韧性回复时间：未破韧时，最后一次实际削韧后经过这么久仍未被继续削韧，
	// 韧性自动回满。与下面的 ExecutableWindowDuration（破韧后的可处决窗口）
	// 互相独立。蓝图内位于 Boss|Combat|Poise 分组，显示名 "Poise Recovery Delay"。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,Category="Boss|Combat|Poise",meta=(AllowPrivateAccess="true", ClampMin="0.1", UIMin="0.1"))
	float PoiseRecoveryDelay = 5.f;

	// 可处决窗口时长：韧性被打空（破韧）后，玩家可以执行处决的时间；
	// 到期后 BOSS 自动回满韧性并退出可处决状态（ExecutableExpired），
	// 处决被打断回窗时重开完整的这个时长。
	// 蓝图内位于 Boss|Combat|Execution 分组，显示名 "Executable Window Duration"。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Combat|Execution", meta=(AllowPrivateAccess="true", ClampMin="0.1", UIMin="0.1"))
	float ExecutableWindowDuration = 5.f;

	// 玩家在传送到 ExecutionPoint 之前发出请求；这是对原始站位的服务端容差，
	// 与 ExecutionPoint 距离 BOSS 多远无关。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Combat|Execution",meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float ExecutionRequestRange = 260.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Anim|Poise",meta=(AllowPrivateAccess="true"))
	TObjectPtr<UAnimMontage> ParryStaggerMontage;

	// 首版只使用 Start / Loop；当前代码不会跳转 Recover。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Anim|Execution",meta=(AllowPrivateAccess="true"))
	TObjectPtr<UAnimMontage> ExecutableMontage;

	// 与主角 Execution_01 配对的 Target_01 Montage。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Anim|Execution",meta=(AllowPrivateAccess="true"))
	TObjectPtr<UAnimMontage> ExecutionTargetMontage;

	// 非致死处决演出结束后，从倒地姿势起立。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
		Category="Boss|Anim|Execution",
		meta=(AllowPrivateAccess="true"))
	TObjectPtr<UAnimMontage> ExecutionGetUpMontage;

	FORCEINLINE UAnimMontage* GetExecutionGetUpMontage() const
	{
		return ExecutionGetUpMontage;
	}

	FORCEINLINE float GetParryStaggerDuration() const{return ParryStaggerDuration;}
	FORCEINLINE float GetPoiseRecoveryDelay() const{return PoiseRecoveryDelay;}
	FORCEINLINE float GetExecutableWindowDuration() const{return ExecutableWindowDuration;}
	FORCEINLINE float GetExecutionRequestRange() const{return ExecutionRequestRange;}
	FORCEINLINE UAnimMontage* GetParryStaggerMontage() const{return ParryStaggerMontage;}
	FORCEINLINE UAnimMontage* GetExecutableMontage() const{return ExecutableMontage;}
	FORCEINLINE UAnimMontage* GetExecutionTargetMontage() const{return ExecutionTargetMontage;}

	UFUNCTION(BlueprintCallable, Category="Boss|Movement")
	void SetBossRotationMode(EBossRotationMode NewMode);

	UFUNCTION(BlueprintPure, Category="Boss|Movement")
	EBossRotationMode GetBossRotationMode() const
	{
		return BossRotationMode;
	}

	FTransform GetExecutionPointTransform() const;

	// 所有负向 Poise 来源的唯一公开入口。
	// Damage 使用正数，例如普通武器传 15、弹反传 50。
	EFirstPoiseDamageResult ApplyPoiseDamage(float Damage,AActor* SourceActor);

	// Executable Ability 仍需要控制这些生命周期工具。
	// 未破韧的削韧走 PoiseRecoveryDelay（韧性回复）；
	// 破韧后 / 处决中止重来走 ExecutableWindowDuration（可处决窗口）。
	void RestartPoiseRecoveryTimer();
	void RestartExecutableWindowTimer();
	void ClearPoiseRecoveryTimer();
	void RestorePoiseToFull();
protected:
	virtual void BeginPlay() override;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="UI", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UEnemyUIComponent> EnemyUIComponent;
	
	// 血量下降时触发受击事件；死亡（NewValue==0）交给死亡能力处理。
	void HandleHealthChanged(const FOnAttributeChangeData& Data);
	
private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UBossCombatComponent> BossCombatComponent;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Boss|Execution",meta=(AllowPrivateAccess="true"))
	TObjectPtr<USceneComponent> ExecutionPoint;

	FTimerHandle PoiseRecoveryTimerHandle;

	// 当前旋转模式：巡逻/追击朝移动方向；近战面目标；忙碌冻结自动旋转。
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly,
		Category="Boss|Movement",
		meta=(AllowPrivateAccess="true"))
	EBossRotationMode BossRotationMode =
		EBossRotationMode::OrientToMovement;

	void HandlePoiseRecoveryExpired();

	// 两个公开 Restart 入口共用的计时器重建；Delay 分别来自
	// PoiseRecoveryDelay（韧性回复）与 ExecutableWindowDuration（可处决窗口）。
	void RestartPoiseTimerInternal(float Delay);
	
	// 原始 GE 数值工具。只供 ABossCharacter 内部调用：
	// 负数只能由 ApplyPoiseDamage 传入，正数只用于恢复。
	void ApplyPoiseDeltaRaw(float Delta, AActor* SourceActor);
};
