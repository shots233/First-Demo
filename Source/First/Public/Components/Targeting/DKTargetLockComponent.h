// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DKTargetLockComponent.generated.h"

class ABaseCharacter;
class ADKCharacter;
class AEnemyCharacter;
class UUserWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTargetLockChanged,AEnemyCharacter*,NewTarget);

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class FIRST_API UDKTargetLockComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDKTargetLockComponent();

	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	// 未锁定：寻找最佳目标；已锁定：手动解除。
	UFUNCTION(BlueprintCallable, Category="Target Lock")
	void ToggleTargetLock();

	// Direction > 0 向屏幕右侧切换；Direction < 0 向左侧切换。
	UFUNCTION(BlueprintCallable, Category="Target Lock")
	void SwitchTarget(float Direction);

	UFUNCTION(BlueprintCallable, Category="Target Lock")
	void ClearTargetLock();

	UFUNCTION(BlueprintPure, Category="Target Lock")
	bool IsTargetLocked() const
	{
		return bLockModeActive && CurrentTarget.IsValid();
	}

	UFUNCTION(BlueprintPure, Category="Target Lock")
	AEnemyCharacter* GetCurrentTarget() const
	{
		return CurrentTarget.Get();
	}

	UFUNCTION(BlueprintPure, Category="Target Lock")
	FVector GetCurrentTargetLocation() const;

	// UI、攻击辅助或其它系统以后都可以监听，不需要反向依赖组件实现。
	UPROPERTY(BlueprintAssignable, Category="Target Lock")
	FOnTargetLockChanged OnTargetLockChanged;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	TArray<AEnemyCharacter*> GatherCandidates(bool bRequireAcquireCone) const;

	bool IsCandidateValid(AEnemyCharacter* Candidate,bool bRequireAcquireCone) const;

	bool IsCharacterDead(const ABaseCharacter* Character) const;
	bool HasLineOfSightTo(AEnemyCharacter* Candidate) const;

	bool ProjectTargetToScreen(AEnemyCharacter* Candidate,FVector2D& OutScreenPosition) const;

	float CalculateAcquireScore(AEnemyCharacter* Candidate) const;

	AEnemyCharacter* FindBestTarget() const;

	void SetCurrentTarget(AEnemyCharacter* NewTarget);
	void UpdateLockedTarget(float DeltaTime);
	void UpdateControlRotation(float DeltaTime);

	void ApplyLockedMovementMode();
	void RestoreMovementMode();

	void ShowTargetIndicator();
	void HideTargetIndicator();
	void UpdateTargetIndicator();

	void ClearTargetLockInternal(const TCHAR* Reason);

	// —— 可在 BP_DKCharacter 的组件详情里调整 ——

	//锁定获取半径（进入此距离内可尝试锁定目标）
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Target Lock|Detection",meta=(AllowPrivateAccess="true", ClampMin="100.0"))
	float AcquireRadius = 2000.f;

	//锁定断开距离（超出此距离自动解除锁定）
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Target Lock|Detection",meta=(AllowPrivateAccess="true", ClampMin="100.0"))
	float BreakDistance = 2600.f;

	//最大获取角度（以度为单位，目标需在屏幕视野中心夹角内）
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Target Lock|Detection",meta=(AllowPrivateAccess="true", ClampMin="0.0", ClampMax="180.0"))
	float MaxAcquireAngleDegrees = 65.f;

	//最大高度差（目标与玩家之间的垂直高度限制）
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Target Lock|Detection",meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float MaxHeightDifference = 600.f;

	//遮挡宽限时间（目标被遮挡后仍保持锁定的秒数）
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Target Lock|Detection",meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float OcclusionGraceTime = 1.f;

	//相机旋转插值速度（锁定目标时相机跟随的平滑速度）
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Target Lock|Camera",meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float CameraRotationInterpSpeed = 8.f;

	//最小相机俯仰角（锁定状态下相机可抬起的最大角度，负值表示上仰）
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Target Lock|Camera",meta=(AllowPrivateAccess="true"))
	float MinCameraPitch = -35.f;

	//最大相机俯仰角（锁定状态下相机可低头的最大角度，正值表示下俯）
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Target Lock|Camera",meta=(AllowPrivateAccess="true"))
	float MaxCameraPitch = 25.f;

	//切换冷却时间（两次切换锁定目标之间的最短间隔）
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Target Lock|Switch",meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float SwitchCooldown = 0.2f;

	//切换屏幕死区（屏幕中心区域像素阈值，用于判定切换方向）
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Target Lock|Switch",meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float SwitchScreenDeadZone = 20.f;

	//目标指示器控件类（UI上显示目标标记的Widget类型）
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Target Lock|UI",meta=(AllowPrivateAccess="true"))
	TSubclassOf<UUserWidget> TargetIndicatorWidgetClass;

	//目标指示器尺寸（UI标记的显示大小，二维向量）
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Target Lock|UI",meta=(AllowPrivateAccess="true"))
	FVector2D TargetIndicatorSize = FVector2D(40.f, 40.f);

	// —— 运行时状态 ——

	UPROPERTY()
	TObjectPtr<ADKCharacter> OwnerCharacter;

	UPROPERTY()
	TWeakObjectPtr<AEnemyCharacter> CurrentTarget;

	UPROPERTY()
	TObjectPtr<UUserWidget> TargetIndicatorWidget;

	float OccludedElapsedTime = 0.f;
	float NextAllowedSwitchTime = 0.f;

	bool bLockModeActive = false;
	bool bAddedTargetLockTag = false;

	bool bMovementSettingsCached = false;
	bool bCachedOrientRotationToMovement = true;
	bool bCachedUseControllerDesiredRotation = false;

		
};
