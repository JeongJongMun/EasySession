// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "EasyExampleCharacter.generated.h"

class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class USpringArmComponent;
struct FInputActionValue;

/**
 * Minimal third person character for the example content, wired up with Enhanced
 * Input the same way the engine's character templates are. Games are expected to
 * replace it with their own character - it only exists so the example maps are
 * playable out of the box.
 *
 * The input assets live in the plugin's example content, so nothing has to be set
 * up in the project. Networked movement comes from ACharacter.
 */
UCLASS(Blueprintable, NotPlaceable)
class AEasyExampleCharacter : public ACharacter
{
	GENERATED_BODY()

public:

	AEasyExampleCharacter();

	//~ Begin APawn Interface
	virtual void NotifyControllerChanged() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	//~ End APawn Interface

protected:

	/** Mapping context added for the locally controlled player. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	/** Move action, expects a 2D axis value (X = right, Y = forward). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	/** Look action, expects a 2D axis value (X = yaw, Y = pitch). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> LookAction;

	/** Jump action. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> JumpAction;

private:

	/** Move relative to the control rotation. */
	void Move(const FInputActionValue& Value);

	/** Turn the view. */
	void Look(const FInputActionValue& Value);

	/** Third person camera boom. */
	UPROPERTY(VisibleAnywhere, Category = "EasySession")
	TObjectPtr<USpringArmComponent> SpringArm;

	/** Third person camera. */
	UPROPERTY(VisibleAnywhere, Category = "EasySession")
	TObjectPtr<UCameraComponent> Camera;
};
