#!/bin/bash

# Clear the terminal screen for a clean presentation (optional)
clear

# Print the diagram using a Here Document
cat << 'EOF'
                     [ Every failed check ends on the current firmware ]
                                            │
                                  Fetch signed manifest
                                            │
                                            ▼
                               ┌─────────────────────────┐
                               │      Newer build?       │
                               └────────────┬────────────┘
                                      yes   │   no
              ┌─────────────────────────────┘   └──────────────────────────┐
              ▼                                                            ▼
  Download to inactive slot                                      Keep current firmware
              │
              ▼
┌─────────────────────────┐
│   Verify signature /    │
│     checksum pass?      │
└────────────┬────────────┘
       yes   │   no
             │   └─────────────────────────────────────────────────────────┐
             ▼                                                             ▼
   Switch active slot                                            Keep current firmware
              │
              ▼
 ┌─────────────────────────┐
 │       Reboot OK?        │
 └────────────┬────────────┘
        yes   │   no
              │   └────────────────────────────────────────────────────────┐
              ▼                                                            ▼
     Commit new firmware                                          Rollback to previous slot
              │                                                            │
              ▼                                                            ▼
[ Run on updated firmware ]                                   [ Run on current firmware ]
EOF

