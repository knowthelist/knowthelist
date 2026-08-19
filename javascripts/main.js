const transitionModes = {
  bass: {
    name: "BASS SWAP",
    title: "Make room for the next kick.",
    description: "The outgoing low end fades as the incoming track takes the floor, keeping the groove clear and powerful."
  },
  vocal: {
    name: "VOCAL HANDOFF",
    title: "Keep the lead in the spotlight.",
    description: "The outgoing midrange steps aside while the incoming vocal rises, so two voices never compete for the room."
  },
  hard: {
    name: "HARD CUT",
    title: "Change the energy in an instant.",
    description: "For a big tempo or style change, the planner makes a clean cut and gets the next track moving without hesitation."
  }
};

document.querySelectorAll(".transition-tab").forEach((tab) => {
  tab.addEventListener("click", () => {
    const mode = transitionModes[tab.dataset.mode];
    document.querySelectorAll(".transition-tab").forEach((item) => {
      item.classList.toggle("active", item === tab);
      item.setAttribute("aria-selected", item === tab ? "true" : "false");
    });
    document.querySelector("#mode-name").textContent = mode.name;
    document.querySelector("#mode-title").textContent = mode.title;
    document.querySelector("#mode-description").textContent = mode.description;
  });
});
