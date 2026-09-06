// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Character/BaseCharacter.h"
#include "EnemyCharacter.generated.h"

class USceneComponent;
/**
 * 
 */
UCLASS()
class FIRST_API AEnemyCharacter : public ABaseCharacter
{
	GENERATED_BODY()
public:
	AEnemyCharacter();

	// 锁敌镜头、遮挡检测和屏幕标记共用这个世界坐标。
	UFUNCTION(BlueprintPure, Category="Target Lock")
	FVector GetTargetLockLocation() const;

private:
	// 可在 BP_Boss 的组件视口中移动到胸口、头部或弱点位置。
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Target Lock",meta=(AllowPrivateAccess="true"))
	TObjectPtr<USceneComponent> TargetLockPoint;
};
