/** @type {import('tailwindcss').Config} */
module.exports = {
  content: ["./content.html"],
  theme: {
    extend: {},
  },
  plugins: [require("daisyui")],
  daisyui: {
    logs: false,
    themes: [
      {
        tawni: {
          primary: "#3DDC97",
          "primary-content": "#0B0F14",
          secondary: "#2A3544",
          "secondary-content": "#E8EEF4",
          accent: "#00BCD4",
          "accent-content": "#0B0F14",
          neutral: "#151C26",
          "neutral-content": "#E8EEF4",
          "base-100": "#0B0F14",
          "base-200": "#151C26",
          "base-300": "#1A2330",
          "base-content": "#E8EEF4",
          info: "#7C9CFF",
          "info-content": "#0B0F14",
          success: "#3DDC97",
          "success-content": "#0B0F14",
          warning: "#FF9100",
          "warning-content": "#0B0F14",
          error: "#F05152",
          "error-content": "#FFFFFF",
        },
      },
    ],
  },
};
