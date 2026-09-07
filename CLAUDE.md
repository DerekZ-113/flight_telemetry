# CLAUDE.md — Flight Telemetry System

## What This Project Is

Embedded flight telemetry system on Raspberry Pi. C++ sensor drivers read barometric pressure (BMP280, I2C), IMU (MPU6050, I2C), and GPS (NEO-6M, UART). Processing engine runs a Kalman filter for altitude fusion, complementary filter for attitude, fault detection, and fixed-rate timing. Streams to a Python/React dashboard styled as an aviation Primary Flight Display (PFD) via ZeroMQ and UDP.

DO-178C-aligned development practices throughout — requirements-based testing, bidirectional traceability, structural coverage (gcov/lcov), static analysis (cppcheck), formal fault modeling.

## Who This Is For

Derek Zhang — AV Validation Specialist at Zoox, MS CS from Northeastern. Building this to demonstrate C++ embedded skills for aerospace/aviation companies (Reliable Robotics, Joby, SpaceX, etc.). The project IS the learning — every component maps to a specific job requirement.

## The One Rule That Matters

**Never write code without a requirement.** Every feature starts with a requirement ID in `docs/requirements.md`, then code, then test, then traceability update. If a feature isn't in requirements.md, it either needs a requirement added first or it doesn't belong. This is the DO-178C discipline and it's non-negotiable for this project.

## DO-178C Workflow — The Loop

Every feature follows this exact sequence:

1. Add requirement to `docs/requirements.md` (REQ-XXXX-NNN format)
2. Add row to `docs/traceability_matrix.md` (empty implementation/test columns)
3. Write the code
4. Update traceability with implementation file/function
5. Write the test (Google Test for C++, pytest for integration)
6. Update traceability with test file/function
7. Run coverage (gcov/lcov), run static analysis (cppcheck)
8. Commit everything together

## Tech Stack

- **C++17** for all embedded/processing code (compiled with g++ or clang++, built with CMake)
- **Python** for data pipeline, integration tests, analysis scripts
- **React** for PFD dashboard
- **Google Test** for C++ unit tests
- **pytest** for cross-language integration tests
- **gcov/lcov** for structural coverage
- **cppcheck** for static analysis
- **ZeroMQ** for local IPC (C++ → Python)
- **UDP** for network telemetry streaming
- **GitHub Actions** for CI/CD
- **YAML** for configuration

## Project Structure

```
src/
  drivers/       ← Sensor driver classes (BMP280, MPU6050, NEO-6M)
  processing/    ← Kalman filter, complementary filter, fault detection
  timing/        ← Fixed-rate loop, jitter measurement
  logging/       ← Binary logger with rotation + replay support
  transport/     ← ZeroMQ publisher, UDP publisher
  replay/        ← Deterministic replay engine
  main.cpp

tests/
  unit/          ← Google Test (C++ units)
  integration/   ← pytest (end-to-end pipeline)
  fault_injection/ ← Parameterized fault scenarios

docs/
  requirements.md         ← Formal requirements (REQ-XXXX-NNN)
  design.md               ← Architecture and design decisions
  traceability_matrix.md  ← REQ → Code → Test mapping
  fault_model.md          ← Failure modes, detection, response
  test_plan.md            ← Test objectives, test cards, pass/fail
  verification_results.md ← Coverage and test result summaries
  display_spec.md         ← PFD instrument specs
  noise_profile.md        ← Sensor characterization results

analysis/       ← Python scripts for noise characterization, flight data replay
pipeline/       ← Python receiver + FastAPI WebSocket server
dashboard/      ← React PFD (attitude indicator, altimeter, heading, vert speed)
config/         ← telemetry_config.yaml
coverage/       ← Generated gcov/lcov reports
static_analysis/ ← Generated cppcheck reports
```

## Coding Conventions

- C++17 standard, compile with `-Wall -Wextra -Wpedantic`
- `const&` for passing structs/objects to functions (no unnecessary copies)
- RAII for resource management (I2C bus handles, file handles)
- Header (.h) and implementation (.cpp) split for all classes
- snake_case for files and variables, PascalCase for classes and structs
- Every class gets its own header/implementation pair
- Comments explain WHY, not WHAT — the code should be readable on its own
- Python code follows standard PEP 8

## Key Architecture Decisions

- **Data source abstraction:** Abstract base class so the processing engine doesn't know if it's reading live sensors or replaying from a binary log. This enables deterministic replay using the same code path as live mode (REQ-LOG-004).
- **Dual transport:** ZeroMQ for local IPC (fast, reliable), UDP for network streaming (mirrors real aircraft-to-ground-station architecture).
- **Kalman filter covariances from measured data:** Noise characterization script runs sensors stationary, computes per-channel statistics, and those measured values feed the Kalman filter. No magic numbers.
- **Fixed-rate timing with clock_nanosleep:** Absolute-time scheduling, not relative sleep. Jitter logged per cycle.
- **Fault detection → DEGRADED state:** Failed sensors don't crash the system. Channel is marked DEGRADED, fault is logged, remaining sensors continue processing.

## Scope Discipline

This project is built incrementally, topic by topic, following the Sprint 1 Plan. When editing files:

- **Only touch what you're asked to touch.** If asked to review `fault_model.md`, don't also rewrite `requirements.md`. Stay in scope.
- **Don't add features, requirements, or code that haven't been discussed.** The brainstorm doc defines the full scope. If something isn't in there, don't add it.
- **Don't restructure the project.** Folder structure and architecture are decided. Improvements to existing files are welcome. Reorganizing everything is not.
- **Review and refine, don't rewrite from scratch.** If a file is 90% good, fix the 10%. Don't delete and start over.
- **Flag issues, don't silently fix them.** If you spot a discrepancy between documents, tell Derek so he understands. Don't quietly align them.
- **Cross-document consistency matters.** Requirement IDs, fault IDs, terminology, and technical details should match across all docs. When reviewing, check that references between documents are consistent.

## Two-Session Workflow

This project runs two Claude sessions in parallel:

- **Claude.ai chat (Opus):** Main session for building, learning, architecture decisions, cold drills, writing new code. This is where Derek learns and decides.
- **Claude Code (Fable):** Review session before commits. Checks cross-document consistency, catches discrepancies, improves wording, validates requirement IDs and traceability. Does NOT add new scope or make architectural changes without asking.

Files are written in the chat session, then reviewed in Claude Code before committing. Claude Code should treat existing files as intentional — they were discussed and decided on in the chat session.

## What NOT to Do

- Don't write code without a requirement in `docs/requirements.md`
- Don't skip the traceability matrix update when adding code or tests
- Don't hardcode sensor parameters — they go in `config/telemetry_config.yaml`
- Don't use raw `new`/`delete` — use RAII, std::vector, std::unique_ptr
- Don't generate code without Derek understanding every line — he needs to explain any line cold in an interview
- Don't skip Google Test for any new C++ module
- Don't mix build artifacts with source (out-of-source build in `build/`)
- Don't go beyond the scope of what's being worked on in the current session
- Don't rewrite files that aren't part of the current task
- Don't add requirements, features, or architecture changes without discussion
- Don't add co-authored-by or co-author tags to commits

## Derek's Learning Context

- Strong in Python, React, full-stack. New to C++ (coursework only, no production experience).
- Has ADHD — works best topic-by-topic, not time-blocked. One component at a time, internalize it, move on.
- The goal is to OWN this code, not just ship it. After each component: close the code, do cold-question drills, verify he can explain everything from memory.
- Previous projects (PatternBank, Sentinel) shipped fast with AI but he didn't internalize the frameworks. This time the tech stack IS the learning target.

## Relevant Obsidian Docs

- `Flight Telemetry System/Project Brainstorm.md` — full project scope, JD-aligned features, phased milestones
- `Flight Telemetry System/Sprint 1 Plan.md` — day-by-day build plan (9/6–9/24)
- `Flight Telemetry System/DO-178C Breakdown.md` — full DO-178C explanation with glossary
- `Reliable Robotics/Interview Prep Master.md` — role mapping and interview prep
- `Reliable Robotics/DO 178C.md` — certification standard notes
