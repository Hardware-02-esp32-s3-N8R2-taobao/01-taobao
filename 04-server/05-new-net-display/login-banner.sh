#!/usr/bin/env bash

if [[ $- == *i* ]] && [[ -n "${SSH_CONNECTION:-}" ]]; then
  if [[ -f /home/zerozero/01-code/05-new-net-display/public-url.txt ]]; then
    echo
    echo "NetDisplay URL: $(/bin/cat /home/zerozero/01-code/05-new-net-display/public-url.txt)"
    echo
  fi
fi
