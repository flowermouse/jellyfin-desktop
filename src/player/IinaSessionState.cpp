#include "IinaSessionState.h"

///////////////////////////////////////////////////////////////////////////////////////////////////
void IinaSessionState::begin()
{
  m_state = State::Launching;
  m_terminal = false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void IinaSessionState::reset()
{
  m_state = State::Idle;
  m_terminal = true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void IinaSessionState::onConnecting()
{
  if (m_terminal)
    return;

  m_state = State::Connecting;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
IinaSessionState::Signal IinaSessionState::onConnected()
{
  if (m_terminal)
    return Signal::None;

  m_state = State::Loading;
  return Signal::None;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
IinaSessionState::Signal IinaSessionState::onFileLoaded()
{
  if (m_terminal)
    return Signal::None;

  // Playback is only announced once the media is actually open; before that IINA may still be
  // launching or resolving the stream.
  if (m_state != State::Launching && m_state != State::Connecting && m_state != State::Loading)
    return Signal::None;

  m_state = State::Playing;
  return Signal::Playing;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
IinaSessionState::Signal IinaSessionState::onPauseChanged(bool paused)
{
  if (m_terminal)
    return Signal::None;

  if (paused)
  {
    if (m_state != State::Playing)
      return Signal::None;

    m_state = State::Paused;
    return Signal::Paused;
  }

  if (m_state != State::Paused)
    return Signal::None;

  m_state = State::Playing;
  return Signal::Playing;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
IinaSessionState::Signal IinaSessionState::onEndFile(const QString& reason)
{
  if (m_terminal)
    return Signal::None;

  // mpv reopens the stream itself after a redirect; playback has not ended.
  if (reason == QLatin1String("redirect"))
    return Signal::None;

  if (reason == QLatin1String("eof"))
    return terminate(Signal::Finished);

  if (reason == QLatin1String("error"))
    return terminate(Signal::Error);

  // "stop" and "quit" both mean playback ended early, whether we asked for it or the user closed
  // the IINA window. An unknown reason is treated the same way: better a spurious stop than
  // leaving Jellyfin stuck in the playing state.
  return terminate(Signal::Canceled);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
IinaSessionState::Signal IinaSessionState::onDisconnected()
{
  return m_terminal ? Signal::None : terminate(Signal::Canceled);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
IinaSessionState::Signal IinaSessionState::onFailed()
{
  return m_terminal ? Signal::None : terminate(Signal::Error);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
IinaSessionState::Signal IinaSessionState::requestStop()
{
  return m_terminal ? Signal::None : terminate(Signal::Canceled);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
IinaSessionState::Signal IinaSessionState::terminate(Signal signal)
{
  m_terminal = true;
  m_state = State::Idle;
  return signal;
}
