// Fill out your copyright notice in the Description page of Project Settings.


#include "Character/BaseCharacter.h"

#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "AbilitySystem/FirstAttributeSet.h"
#include "DataAssets/StartUpData/DataAsset_StartUpDataBase.h"

// Sets default values
ABaseCharacter::ABaseCharacter()
{
	//彻底关闭了这个Actor的Tick功能
	PrimaryActorTick.bCanEverTick = false;
	//确保Actor刚诞生时Tick是关闭的
	PrimaryActorTick.bStartWithTickEnabled = false;
	
	FirstAbilitySystemComponent = CreateDefaultSubobject<UFirstAbilitySystemComponent>(TEXT("FirstAbilitySystemComponent"));
	// AttributeSet 作为 ASC 所在角色的默认子对象创建，生命周期与角色一致。
	FirstAttributeSet = CreateDefaultSubobject<UFirstAttributeSet>(TEXT("FirstAttributeSet"));
}

UAbilitySystemComponent* ABaseCharacter::GetAbilitySystemComponent() const
{
	return FirstAbilitySystemComponent;
}

void ABaseCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	
	// 单机/服务器通常走这里。完成 ActorInfo 后才允许读取 StartUpData。
	InitializeAbilitySystem();
	GiveStartupData();
}

void ABaseCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	
	// 当前作业把 ASC 放在 Character 上，单机时这个回调通常不是关键路径；
	// 仍保留它，让以后把 ASC 迁移到 PlayerState 时有清晰的客户端初始化入口。
	InitializeAbilitySystem();
	GiveStartupData();
}


// Called to bind functionality to input
void ABaseCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

}

void ABaseCharacter::InitializeAbilitySystem()
{
	if (!FirstAbilitySystemComponent)
	{
		return;
	}
	
	// 当前作业 ASC 放在 Character 上，OwnerActor 和 AvatarActor 都使用 this。
	// 以后如果迁移到 PlayerState，再调整 OwnerActor。
	FirstAbilitySystemComponent->InitAbilityActorInfo(this, this);
	
	UE_LOG(LogTemp, Warning, TEXT("[%s] ASC initialized"), *GetNameSafe(this));
}

void ABaseCharacter::GiveStartupData()
{
	if (bStartupDataGiven || CharacterStartUpData.IsNull() || !FirstAbilitySystemComponent)
	{
		return;
	}
	
	if (UDataAsset_StartUpDataBase* LoadedData = CharacterStartUpData.LoadSynchronous())
	{
		// TSoftObjectPtr 允许蓝图引用数据资产但避免构造阶段强加载；
		// 角色真正被控制后才同步加载一次，学习项目中更易理解和验证。
		LoadedData->GiveToAbilitySystemComponent(FirstAbilitySystemComponent);
		bStartupDataGiven = true;
	}
}

