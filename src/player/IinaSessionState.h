#ifndef IINASESSIONSTATE_H
#define IINASESSIONSTATE_H

#include <QString>

///////////////////////////////////////////////////////////////////////////////////////////////////
// The lifecycle of a single external playback session, with no Qt objects, sockets or processes
// attached so the transitions can be tested directly.
//
//   Idle -> Launching -> Connecting -> Loading -> Playing <-> Paused
//                                              -> Finished | Canceled | Error -> Idle
//
// Every input returns the signal the owner should emit, or Signal::None. At most one terminal
// signal is ever produced per session; everything arriving afterwards (late events from a socket
// that is being torn down, for example) is swallowed.
class IinaSessionState
{
public:
  enum class State
  {
    Idle,
    Launching,
    Connecting,
    Loading,
    Playing,
    Paused,
  };

  enum class Signal
  {
    None,
    Playing,
    Paused,
    Finished,
    Canceled,
    Error,
  };

  State state() const { return m_state; }
  bool isTerminal() const { return m_terminal; }

  void begin();
  void reset();

  void onConnecting();
  Signal onConnected();

  // mpv's file-loaded / playback-restart.
  Signal onFileLoaded();
  Signal onPauseChanged(bool paused);
  Signal onEndFile(const QString& reason);
  Signal onDisconnected();
  Signal onFailed();
  Signal requestStop();

private:
  Signal terminate(Signal signal);

  State m_state = State::Idle;
  bool m_terminal = true;
};

#endif // IINASESSIONSTATE_H
