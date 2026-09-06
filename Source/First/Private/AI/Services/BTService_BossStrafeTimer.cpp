// Fill out your copyright notice in the Description page of Project Settings.


#include "AI/Services/BTService_BossStrafeTimer.h"

#include "BehaviorTree/BlackboardComponent.h"


struct FBossStrafeTimerMemory
{
	float ElapsedTime = 0.f;
};

UBTService_BossStrafeTimer::UBTService_BossStrafeTimer()
{
	NodeName = TEXT("Boss Strafe Timer");
}

uint16 UBTService_BossStrafeTimer::GetInstanceMemorySize() const
{
	return sizeof(FBossStrafeTimerMemory);
}

void UBTService_BossStrafeTimer::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB)
	{
		return;
	}

	FBossStrafeTimerMemory* Memory = reinterpret_cast<FBossStrafeTimerMemory*>(NodeMemory);

	const bool bInStrafe = BB->GetValueAsBool(TEXT("bInStrafeRange"));
	// bIsBusy 是索敌手册里 UpdateBossState 聚合的"忙碌"键（出招/拔剑中）。
	// 如果你还没做 bIsBusy，就把这一行和下面的 && 条件删掉。
	const bool bBusy = BB->GetValueAsBool(TEXT("bIsBusy"));

	// 1. 不在侧移范围，或正在出招：计时清零，允许下次重新侧移。
	if (!bInStrafe || bBusy)
	{
		Memory->ElapsedTime = 0.f;
		BB->SetValueAsBool(TEXT("bStrafeTimeout"), false);
		return;
	}

	// 2. 持续侧移：累计时间，超时置 true（行为树停止侧移、转为靠近）。
	Memory->ElapsedTime += DeltaSeconds;
	BB->SetValueAsBool(
		TEXT("bStrafeTimeout"),
		Memory->ElapsedTime >= StrafeMaxDuration);
}
