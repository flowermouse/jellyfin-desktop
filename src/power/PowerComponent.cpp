//
// Created by Tobias Hieta on 25/03/15.
//
#include "PowerComponent.h"
#include "input/InputComponent.h"
#include "settings/SettingsComponent.h"
#include "player/PlayerComponent.h"

#include "PowerComponentMac.h"

/////////////////////////////////////////////////////////////////////////////////////////
PowerComponent& PowerComponent::Get()
{
  static PowerComponentMac instance;
  return instance;
}

/////////////////////////////////////////////////////////////////////////////////////////
bool PowerComponent::componentInitialize()
{
  return true;
}

/////////////////////////////////////////////////////////////////////////////////////////
void PowerComponent::setScreensaverEnabled(bool enabled)
{
  if (enabled)
  {
    qDebug() << "Enabling OS screensaver";
    doEnableScreensaver();
  }
  else
  {
    qDebug() << "Disabling OS screensaver";
    doDisableScreensaver();
  }
}

/////////////////////////////////////////////////////////////////////////////////////////
void PowerComponent::componentPostInitialize()
{
  InputComponent::Get().registerHostCommand("poweroff", this, "PowerOff");
  InputComponent::Get().registerHostCommand("reboot", this, "Reboot");
  InputComponent::Get().registerHostCommand("suspend", this, "Suspend");

  connect(&PlayerComponent::Get(), &PlayerComponent::playbackStateChanged,
          this, [this](const QString& state) { setScreensaverEnabled(state != "Playing"); });
  connect(&PlayerComponent::Get(), &PlayerComponent::playbackStopped,
          this, [this](bool) { setScreensaverEnabled(true); });
}

/////////////////////////////////////////////////////////////////////////////////////////
bool PowerComponent::checkCap(PowerCapabilities capability)
{
  if (!SettingsComponent::Get().value(SETTINGS_SECTION_MAIN, "showPowerOptions").toBool())
    return false;

  return (getPowerCapabilities() & capability);
}


