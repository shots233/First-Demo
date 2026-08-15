// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "GameFramework/Character.h"
#include "BaseCharacter.generated.h"

class UDataAsset_StartUpDataBase;
class UFirstAttributeSet;
class UFirstAbilitySystemComponent;
class UAbilitySystemComponent;

UCLASS()
class FIRST_API ABaseCharacter : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	ABaseCharacter();

	// 作用：实现 GAS 查询接口，使通用函数能从角色 Actor 找到它的 ASC。
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;


protected:
	
	// 作用：角色被控制器占有时初始化 ASC 的 Owner/Avatar 上下文，并授予出生数据。
	virtual void PossessedBy(AController* NewController) override;
	// 作用：客户端同步到 PlayerState 后再次确保 ASC 上下文有效，为以后多人扩展保留入口。
	virtual void OnRep_PlayerState() override;
	// 作用：保留 ACharacter 的输入设置链；具体 Ability 输入绑定由子类完成。
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	
	// 两个函数分开：先建立 Owner/Avatar 上下文，再应用初始属性和授予 Ability。
	// 先后顺序不能反，因为 GiveAbility / GE 都需要有效的 ActorInfo。
	// 作用：建立 ASC 与当前角色的 ActorInfo 关系，后续 Ability 和 Effect 才知道作用对象是谁。
	void InitializeAbilitySystem();
	// 作用：读取角色数据资产，应用初始 GameplayEffect 并授予出生需要的 Ability。
	void GiveStartupData();
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AbilitySystem")
	TObjectPtr<UFirstAbilitySystemComponent> FirstAbilitySystemComponent;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AbilitySystem")
	TObjectPtr<UFirstAttributeSet> FirstAttributeSet;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Character Data")
	TSoftObjectPtr<UDataAsset_StartUpDataBase> CharacterStartUpData;
	
private:
	// PossessedBy 与 OnRep_PlayerState 都可能调用初始化；这个标记防止重复授予 Ability。
	bool bStartupDataGiven = false;
	
public:
	
	// 作用：为 C++ 或蓝图派生类提供本项目类型的 ASC 快捷访问入口。
	FORCEINLINE UFirstAbilitySystemComponent* GetFirstAbilitySystemComponent() const
	{
		return FirstAbilitySystemComponent;
	}

	// 作用：为角色逻辑提供本项目 AttributeSet 的快捷访问入口，避免重复 Cast。
	FORCEINLINE UFirstAttributeSet* GetFirstAttributeSet() const
	{
		return FirstAttributeSet;
	}
	
};
