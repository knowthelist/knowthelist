# Local Engine AI Coding Constraints

## 1. Token Safety & Output Caps
- NEVER rewrite a whole file or large sections of code.
- You must break all requested tasks into micro-steps (maximum 15-20 lines of changes per turn).
- If a refactoring task requires massive modifications, explain your plan first, modify ONE function, then ask the user: "Should I proceed to the next step?".
- Ensure any unified diff patch you send contains less than 200 total output tokens.

Follow these strict rules until you are done: 1. CONTINUITY: If you hit a token limit or stop mid-file, do not wait for me. Automatically generate the next tool call to continue writing the remaining code. 2. COMPLETION: Implement every single remaining step of the refactor plan. Do not leave placeholder comments, TODOs, or mock data. 3. COMPILATION: Once all code is written, execute the project compile/build command using your bash tool. If there are compilation or type errors, fix them immediately. 4. DONE CRITERIA: You are only allowed to stop and tell me you are finished when: - Every step of our plan is implemented. - The project compiles with 0 errors. Execute the next step of the plan now.