;;; event-emacs-theme.el --- Event Horizon wallpaper-derived color theme for Emacs

;; Copyright (C) 2025 Event Horizon

;; Author: Event Horizon
;; Version: 1.3
;; Package-Requires: ((emacs "24.1"))
;; Keywords: faces

;;; Commentary:

;; A wallpaper-derived theme built from the Matugen Material 3 palette,
;; with the shell's 16-color base16 palette used for syntax highlighting:
;; - Rich, high-contrast syntax colors from the wallpaper 16-color palette
;; - Clear source block distinction with refined backgrounds
;; - Org-mode styling with hidden asterisks
;; - Modern visual hierarchy

;;; Code:

(deftheme event-emacs "Event Horizon wallpaper-derived theme using Matugen variables with the base16 color palette.")

;; Define all the color variables (replaced by template processor)
(let* ((bg "{{colors.background.default.hex}}")
      (err "{{event16.color1.default.hex}}")  ; Red from base16
      (err-container "{{colors.error_container.default.hex}}")
      (on-background "{{colors.on_background.default.hex}}")
      (on-err "{{colors.on_error.default.hex}}")
      (on-err-container "{{colors.on_error_container.default.hex}}")
      (on-primary "{{colors.on_primary.default.hex}}")
      (on-primary-container "{{colors.on_primary_container.default.hex}}")
      (on-secondary "{{colors.on_secondary.default.hex}}")
      (on-secondary-container "{{colors.on_secondary_container.default.hex}}")
      (on-surface "{{colors.on_surface.default.hex}}")
      (on-surface-variant "{{colors.on_surface_variant.default.hex}}")
      (on-tertiary "{{colors.on_tertiary.default.hex}}")
      (on-tertiary-container "{{colors.on_tertiary_container.default.hex}}")
      (outline-color "{{colors.outline.default.hex}}")
      (outline-variant "{{colors.outline_variant.default.hex}}")
      (primary "{{colors.primary.default.hex}}")
      (primary-container "{{colors.primary_container.default.hex}}")
      (secondary "{{colors.secondary.default.hex}}")
      (secondary-container "{{colors.secondary_container.default.hex}}")
      (shadow "{{colors.shadow.default.hex}}")
      (surface "{{colors.surface.default.hex}}")
      (surface-container "{{colors.surface_container_high.default.hex}}")
      (surface-container-high "{{colors.surface_container_highest.default.hex}}")
      (surface-container-highest "{{colors.surface_container_high.default.hex}}")
      (surface-container-low "{{colors.surface_container_low.default.hex}}")
      (surface-container-lowest "{{colors.surface_container_lowest.default.hex}}")
      (surface-variant "{{colors.surface_variant.default.hex}}")
      (tertiary "{{colors.tertiary.default.hex}}")
      (tertiary-container "{{colors.tertiary_container.default.hex}}")

      ;; Enhanced base16 colors for better syntax highlighting
      (event-red "{{event16.color1.default.hex}}")          ; Bright red
      (event-red-alt "{{event16.color9.default.hex}}")      ; Alternative red
      (event-green "{{event16.color2.default.hex}}")        ; Vibrant green
      (event-green-bright "{{event16.color10.default.hex}}") ; Bright green
      (event-yellow "{{event16.color3.default.hex}}")       ; Warm yellow
      (event-yellow-bright "{{event16.color11.default.hex}}") ; Bright yellow
      (event-blue "{{event16.color4.default.hex}}")         ; Blue-green
      (event-magenta "{{event16.color5.default.hex}}")      ; Teal-magenta
      (event-cyan "{{event16.color6.default.hex}}")         ; Bright cyan
      (event-cyan-bright "{{event16.color12.default.hex}}") ; Brightest cyan
      (event-cyan-dark "{{event16.color13.default.hex}}")   ; Dark cyan
      (event-teal "{{event16.color14.default.hex}}")        ; Dark teal
      (event-fg "{{event16.color7.default.hex}}")           ; Light foreground
      (event-gray "{{event16.color8.default.hex}}")         ; Gray
      (event-white "{{event16.color15.default.hex}}")       ; White

      ;; Map success colors to green
      (success "{{event16.color2.default.hex}}")
      (on-success "{{colors.on_tertiary.default.hex}}")
      (success-container "{{colors.tertiary_container.default.hex}}")
      (on-success-container "{{colors.on_tertiary_container.default.hex}}")

      ;; Map fixed colors
      (primary-fixed "{{colors.primary_fixed.default.hex}}")
      (primary-fixed-dim "{{colors.primary_fixed_dim.default.hex}}")
      (secondary-fixed "{{colors.secondary_fixed.default.hex}}")
      (secondary-fixed-dim "{{colors.secondary_fixed_dim.default.hex}}")
      (tertiary-fixed "{{colors.tertiary_fixed.default.hex}}")
      (tertiary-fixed-dim "{{colors.tertiary_fixed_dim.default.hex}}")
      (on-primary-fixed "{{colors.on_primary_fixed.default.hex}}")
      (on-primary-fixed-variant "{{colors.on_primary_fixed_variant.default.hex}}")
      (on-secondary-fixed "{{colors.on_secondary_fixed.default.hex}}")
      (on-secondary-fixed-variant "{{colors.on_secondary_fixed_variant.default.hex}}")
      (on-tertiary-fixed "{{colors.on_tertiary_fixed.default.hex}}")
      (on-tertiary-fixed-variant "{{colors.on_tertiary_fixed_variant.default.hex}}")

      ;; Map inverse colors
      (inverse-on-surface "{{colors.inverse_on_surface.default.hex}}")
      (inverse-primary "{{colors.inverse_primary.default.hex}}")
      (inverse-surface "{{colors.inverse_surface.default.hex}}")

      ;; Terminal colors from base16
      (term0 "{{event16.color0.default.hex}}")
      (term1 "{{event16.color1.default.hex}}")
      (term2 "{{event16.color2.default.hex}}")
      (term3 "{{event16.color3.default.hex}}")
      (term4 "{{event16.color4.default.hex}}")
      (term5 "{{event16.color5.default.hex}}")
      (term6 "{{event16.color6.default.hex}}")
      (term7 "{{event16.color7.default.hex}}")
      (term8 "{{event16.color8.default.hex}}")
      (term9 "{{event16.color9.default.hex}}")
      (term10 "{{event16.color10.default.hex}}")
      (term11 "{{event16.color11.default.hex}}")
      (term12 "{{event16.color12.default.hex}}")
      (term13 "{{event16.color13.default.hex}}")
      (term14 "{{event16.color14.default.hex}}")
      (term15 "{{event16.color15.default.hex}}"))

  (custom-theme-set-faces
   'event-emacs
   ;; Basic faces
   `(default ((t (:background ,bg :foreground ,on-background))))
   `(cursor ((t (:background ,event-cyan-bright))))
   `(highlight ((t (:background ,primary-container :foreground ,on-primary-container))))
   `(region ((t (:background ,primary-container :foreground ,event-cyan-bright :extend t))))
   `(secondary-selection ((t (:background ,secondary-container :foreground ,on-secondary-container :extend t))))
   `(isearch ((t (:background ,event-yellow :foreground ,bg :weight bold))))
   `(lazy-highlight ((t (:background ,secondary-container :foreground ,event-yellow-bright))))
   `(vertical-border ((t (:foreground ,surface-variant))))
   `(border ((t (:background ,surface-variant :foreground ,surface-variant))))
   `(fringe ((t (:background ,surface :foreground ,event-gray))))
   `(shadow ((t (:foreground ,event-gray))))
   `(link ((t (:foreground ,event-cyan-bright :underline t))))
   `(link-visited ((t (:foreground ,event-magenta :underline t))))
   `(success ((t (:foreground ,success))))
   `(warning ((t (:foreground ,event-yellow))))
   `(error ((t (:foreground ,err))))
   `(match ((t (:background ,event-yellow :foreground ,bg :weight bold))))

   ;; Font-lock - enhanced with matugen base16 colors for vibrant syntax highlighting
   `(font-lock-builtin-face ((t (:foreground ,event-cyan-bright))))
   `(font-lock-comment-face ((t (:foreground ,event-gray :slant italic))))
   `(font-lock-comment-delimiter-face ((t (:foreground ,outline-variant))))
   `(font-lock-constant-face ((t (:foreground ,event-yellow-bright :weight bold))))
   `(font-lock-doc-face ((t (:foreground ,event-fg :slant italic))))
   `(font-lock-function-name-face ((t (:foreground ,event-cyan :weight bold))))
   `(font-lock-keyword-face ((t (:foreground ,event-red-alt :weight bold))))
   `(font-lock-string-face ((t (:foreground ,event-green))))
   `(font-lock-type-face ((t (:foreground ,event-yellow))))
   `(font-lock-variable-name-face ((t (:foreground ,event-fg))))
   `(font-lock-warning-face ((t (:foreground ,err :weight bold))))
   `(font-lock-preprocessor-face ((t (:foreground ,event-teal))))
   `(font-lock-negation-char-face ((t (:foreground ,event-red))))

   ;; Show paren
   `(show-paren-match ((t (:background ,primary-container :foreground ,event-cyan-bright :weight bold))))
   `(show-paren-mismatch ((t (:background ,err-container :foreground ,on-err-container :weight bold))))

   ;; Mode line - improved status bar styling
   `(mode-line ((t (:background ,surface-container :foreground ,on-surface :box nil))))
   `(mode-line-inactive ((t (:background ,surface :foreground ,event-gray :box nil))))
   `(mode-line-buffer-id ((t (:foreground ,event-cyan :weight bold))))
   `(mode-line-emphasis ((t (:foreground ,event-cyan :weight bold))))
   `(mode-line-highlight ((t (:foreground ,event-cyan-bright :box nil))))

   ;; Improved Source blocks - seamless integration
   `(org-block ((t (:background ,surface-container-low :extend t :inherit fixed-pitch))))
   `(org-block-begin-line ((t (:background ,surface-container-low :foreground ,event-teal :extend t :slant italic :inherit fixed-pitch))))
   `(org-block-end-line ((t (:background ,surface-container-low :foreground ,event-teal :extend t :slant italic :inherit fixed-pitch))))
   `(org-code ((t (:background ,surface-container-low :foreground ,event-yellow-bright :inherit fixed-pitch))))
   `(org-verbatim ((t (:background ,surface-container-low :foreground ,event-cyan :inherit fixed-pitch))))
   `(org-meta-line ((t (:foreground ,event-gray :slant italic))))

   ;; Org mode with enhanced colors and hidden asterisks
   `(org-level-1 ((t (:foreground ,event-cyan :weight bold :height 1.2))))
   `(org-level-2 ((t (:foreground ,event-blue :weight bold :height 1.1))))
   `(org-level-3 ((t (:foreground ,event-magenta :weight bold))))
   `(org-level-4 ((t (:foreground ,event-green :weight bold))))
   `(org-level-5 ((t (:foreground ,event-yellow :weight bold))))
   `(org-level-6 ((t (:foreground ,event-cyan-bright :weight bold))))
   `(org-level-7 ((t (:foreground ,event-red-alt :weight bold))))
   `(org-level-8 ((t (:foreground ,event-teal :weight bold))))
   `(org-document-title ((t (:foreground ,event-cyan :weight bold :height 1.3))))
   `(org-document-info ((t (:foreground ,event-blue))))
   `(org-todo ((t (:foreground ,event-red :weight bold))))
   `(org-done ((t (:foreground ,success :weight bold))))
   `(org-headline-done ((t (:foreground ,event-gray))))
   `(org-hide ((t (:foreground ,bg))))
   `(org-ellipsis ((t (:foreground ,event-blue :underline nil))))
   `(org-table ((t (:foreground ,event-magenta :inherit fixed-pitch))))
   `(org-formula ((t (:foreground ,event-yellow-bright :inherit fixed-pitch))))
   `(org-checkbox ((t (:foreground ,event-cyan :weight bold :inherit fixed-pitch))))
   `(org-date ((t (:foreground ,event-teal :underline t))))
   `(org-special-keyword ((t (:foreground ,event-gray :slant italic))))
   `(org-tag ((t (:foreground ,event-gray :weight normal))))

   ;; Magit with enhanced diff colors
   `(magit-section-highlight ((t (:background ,surface-container-low))))
   `(magit-diff-hunk-heading ((t (:background ,surface-container :foreground ,event-gray))))
   `(magit-diff-hunk-heading-highlight ((t (:background ,surface-container-high :foreground ,on-surface))))
   `(magit-diff-context ((t (:foreground ,event-gray))))
   `(magit-diff-context-highlight ((t (:background ,surface-container-low :foreground ,on-surface))))
   `(magit-diff-added ((t (:background ,success-container :foreground ,event-green-bright))))
   `(magit-diff-added-highlight ((t (:background ,success-container :foreground ,event-green-bright :weight bold))))
   `(magit-diff-removed ((t (:background ,err-container :foreground ,event-red-alt))))
   `(magit-diff-removed-highlight ((t (:background ,err-container :foreground ,event-red-alt :weight bold))))
   `(magit-hash ((t (:foreground ,event-gray))))
   `(magit-branch-local ((t (:foreground ,event-blue :weight bold))))
   `(magit-branch-remote ((t (:foreground ,event-cyan :weight bold))))

   ;; Company
   `(company-tooltip ((t (:background ,surface-container :foreground ,on-surface))))
   `(company-tooltip-selection ((t (:background ,primary-container :foreground ,event-cyan-bright))))
   `(company-tooltip-common ((t (:foreground ,event-cyan))))
   `(company-tooltip-common-selection ((t (:foreground ,event-cyan-bright :weight bold))))
   `(company-tooltip-annotation ((t (:foreground ,event-yellow))))
   `(company-scrollbar-fg ((t (:background ,event-cyan))))
   `(company-scrollbar-bg ((t (:background ,surface-variant))))
   `(company-preview ((t (:foreground ,event-gray :slant italic))))
   `(company-preview-common ((t (:foreground ,event-cyan :slant italic))))

   ;; Ido
   `(ido-first-match ((t (:foreground ,event-cyan :weight bold))))
   `(ido-only-match ((t (:foreground ,event-green :weight bold))))
   `(ido-subdir ((t (:foreground ,event-blue))))
   `(ido-indicator ((t (:foreground ,event-red))))
   `(ido-virtual ((t (:foreground ,event-gray))))

   ;; Helm
   `(helm-selection ((t (:background ,primary-container :foreground ,event-cyan-bright))))
   `(helm-match ((t (:foreground ,event-cyan :weight bold))))
   `(helm-source-header ((t (:background ,surface-container-high :foreground ,event-cyan :weight bold :height 1.1))))
   `(helm-candidate-number ((t (:foreground ,event-yellow :weight bold))))
   `(helm-ff-directory ((t (:foreground ,event-cyan :weight bold))))
   `(helm-ff-file ((t (:foreground ,on-surface))))
   `(helm-ff-executable ((t (:foreground ,event-green))))

   ;; corfu
   `(corfu-default ((t (:background ,surface-container :foreground ,on-surface))))
   `(corfu-current ((t (:background ,primary-container :foreground ,event-cyan-bright))))

   ;; Which-key
   `(which-key-key-face ((t (:foreground ,event-cyan :weight bold))))
   `(which-key-separator-face ((t (:foreground ,outline-variant))))
   `(which-key-command-description-face ((t (:foreground ,on-surface))))
   `(which-key-group-description-face ((t (:foreground ,event-blue))))
   `(which-key-special-key-face ((t (:foreground ,event-yellow :weight bold))))

   ;; Line numbers
   `(line-number ((t (:foreground ,event-gray :inherit fixed-pitch))))
   `(line-number-current-line ((t (:foreground ,event-cyan :weight bold :inherit fixed-pitch))))

   ;; Parenthesis matching
   `(sp-show-pair-match-face ((t (:background ,primary-container :foreground ,event-cyan-bright))))
   `(sp-show-pair-mismatch-face ((t (:background ,err-container :foreground ,on-err-container))))

   ;; Rainbow delimiters - vibrant colors
   `(rainbow-delimiters-depth-1-face ((t (:foreground ,event-cyan))))
   `(rainbow-delimiters-depth-2-face ((t (:foreground ,event-yellow))))
   `(rainbow-delimiters-depth-3-face ((t (:foreground ,event-green))))
   `(rainbow-delimiters-depth-4-face ((t (:foreground ,event-blue))))
   `(rainbow-delimiters-depth-5-face ((t (:foreground ,event-magenta))))
   `(rainbow-delimiters-depth-6-face ((t (:foreground ,event-cyan-bright))))
   `(rainbow-delimiters-depth-7-face ((t (:foreground ,event-yellow-bright))))
   `(rainbow-delimiters-depth-8-face ((t (:foreground ,event-green-bright))))
   `(rainbow-delimiters-depth-9-face ((t (:foreground ,event-red-alt))))
   `(rainbow-delimiters-mismatched-face ((t (:foreground ,err :weight bold))))
   `(rainbow-delimiters-unmatched-face ((t (:foreground ,err :weight bold))))

   ;; Dired
   `(dired-directory ((t (:foreground ,event-cyan :weight bold))))
   `(dired-ignored ((t (:foreground ,event-gray))))
   `(dired-flagged ((t (:foreground ,event-red))))
   `(dired-marked ((t (:foreground ,event-yellow :weight bold))))
   `(dired-symlink ((t (:foreground ,event-magenta :slant italic))))
   `(dired-header ((t (:foreground ,event-cyan :weight bold :height 1.1))))

   ;; Terminal colors
   `(term-color-black ((t (:foreground ,term0 :background ,term0))))
   `(term-color-red ((t (:foreground ,term1 :background ,term1))))
   `(term-color-green ((t (:foreground ,term2 :background ,term2))))
   `(term-color-yellow ((t (:foreground ,term3 :background ,term3))))
   `(term-color-blue ((t (:foreground ,term4 :background ,term4))))
   `(term-color-magenta ((t (:foreground ,term5 :background ,term5))))
   `(term-color-cyan ((t (:foreground ,term6 :background ,term6))))
   `(term-color-white ((t (:foreground ,term7 :background ,term7))))

   ;; EShell
   `(eshell-prompt ((t (:foreground ,event-cyan :weight bold))))
   `(eshell-ls-directory ((t (:foreground ,event-cyan :weight bold))))
   `(eshell-ls-symlink ((t (:foreground ,event-magenta :slant italic))))
   `(eshell-ls-executable ((t (:foreground ,event-green))))
   `(eshell-ls-archive ((t (:foreground ,event-yellow))))
   `(eshell-ls-backup ((t (:foreground ,event-gray))))
   `(eshell-ls-clutter ((t (:foreground ,event-red))))
   `(eshell-ls-missing ((t (:foreground ,event-red))))
   `(eshell-ls-product ((t (:foreground ,on-surface-variant))))
   `(eshell-ls-readonly ((t (:foreground ,event-gray))))
   `(eshell-ls-special ((t (:foreground ,event-blue))))
   `(eshell-ls-unreadable ((t (:foreground ,event-gray))))

   ;; Improved markdown mode
   `(markdown-header-face ((t (:foreground ,event-cyan :weight bold))))
   `(markdown-header-face-1 ((t (:foreground ,event-cyan :weight bold :height 1.2))))
   `(markdown-header-face-2 ((t (:foreground ,event-blue :weight bold :height 1.1))))
   `(markdown-header-face-3 ((t (:foreground ,event-magenta :weight bold))))
   `(markdown-header-face-4 ((t (:foreground ,event-green :weight bold))))
   `(markdown-inline-code-face ((t (:foreground ,event-yellow-bright :background ,surface-container-low :inherit fixed-pitch))))
   `(markdown-code-face ((t (:background ,surface-container-low :extend t :inherit fixed-pitch))))
   `(markdown-pre-face ((t (:background ,surface-container-low :inherit fixed-pitch))))
   `(markdown-table-face ((t (:foreground ,event-magenta :inherit fixed-pitch))))

   ;; Web mode
   `(web-mode-html-tag-face ((t (:foreground ,event-cyan))))
   `(web-mode-html-tag-bracket-face ((t (:foreground ,event-gray))))
   `(web-mode-html-attr-name-face ((t (:foreground ,event-yellow))))
   `(web-mode-html-attr-value-face ((t (:foreground ,event-green))))
   `(web-mode-css-selector-face ((t (:foreground ,event-cyan))))
   `(web-mode-css-property-name-face ((t (:foreground ,event-blue))))
   `(web-mode-css-string-face ((t (:foreground ,event-green))))

   ;; Flycheck
   `(flycheck-error ((t (:underline (:style wave :color ,err)))))
   `(flycheck-warning ((t (:underline (:style wave :color ,event-yellow)))))
   `(flycheck-info ((t (:underline (:style wave :color ,event-blue)))))
   `(flycheck-fringe-error ((t (:foreground ,err))))
   `(flycheck-fringe-warning ((t (:foreground ,event-yellow))))
   `(flycheck-fringe-info ((t (:foreground ,event-blue))))

   ;; Mini-buffer customization
   `(minibuffer-prompt ((t (:foreground ,event-cyan :weight bold))))

   ;; Improved search highlighting
   `(lsp-face-highlight-textual ((t (:background ,primary-container :foreground ,event-cyan-bright :weight bold))))
   `(lsp-face-highlight-read ((t (:background ,secondary-container :foreground ,event-yellow-bright :weight bold))))
   `(lsp-face-highlight-write ((t (:background ,tertiary-container :foreground ,event-green-bright :weight bold))))

   ;; Info and help modes
   `(info-title-1 ((t (:foreground ,event-cyan :weight bold :height 1.3))))
   `(info-title-2 ((t (:foreground ,event-blue :weight bold :height 1.2))))
   `(info-title-3 ((t (:foreground ,event-magenta :weight bold :height 1.1))))
   `(info-title-4 ((t (:foreground ,event-green :weight bold))))
   `(Info-quoted ((t (:foreground ,event-yellow))))
   `(info-menu-header ((t (:foreground ,event-cyan :weight bold))))
   `(info-menu-star ((t (:foreground ,event-cyan))))
   `(info-node ((t (:foreground ,event-blue :weight bold))))

   ;; Tabs
   `(tab-bar ((t (:background ,surface-container :foreground ,on-surface :box nil))))
   `(tab-bar-tab ((t (:background ,surface-container-high :foreground ,event-cyan :weight bold :box nil))))
   `(tab-bar-tab-inactive ((t (:background ,surface :foreground ,event-gray :box nil))))

   `(tab-line ((t (:background ,surface-container :foreground ,on-surface :box nil))))
   `(tab-line-tab ((t (:background ,surface :foreground ,event-gray :box nil))))
   `(tab-line-tab-current ((t (:background ,surface-container-high :foreground ,event-cyan :weight bold :box nil))))
   `(tab-line-tab-inactive ((t (:background ,surface :foreground ,event-gray :box nil))))
   `(tab-line-highlight ((t (:background ,surface-container-highest :foreground ,event-cyan-bright))))

   `(centaur-tabs-default ((t (:background ,surface-container :foreground ,on-surface))))
   `(centaur-tabs-selected ((t (:background ,surface-container-high :foreground ,event-cyan :weight bold))))
   `(centaur-tabs-unselected ((t (:background ,surface :foreground ,event-gray))))
   `(centaur-tabs-selected-modified ((t (:background ,surface-container-high :foreground ,event-yellow :weight bold))))
   `(centaur-tabs-unselected-modified ((t (:background ,surface :foreground ,event-yellow))))
   `(centaur-tabs-active-bar-face ((t (:background ,event-cyan))))

   ;; Fixed-pitch faces
   `(fixed-pitch ((t (:family "monospace"))))
   `(fixed-pitch-serif ((t (:family "monospace serif"))))

   ;; Variable-pitch face
   `(variable-pitch ((t (:family "sans serif"))))
   ))

;; Add org-mode hooks for hiding leading stars
(with-eval-after-load 'org
  (setq org-hide-leading-stars t)
  (setq org-startup-indented t))

;;;###autoload
(when load-file-name
  (add-to-list 'custom-theme-load-path
               (file-name-as-directory (file-name-directory load-file-name))))

(provide-theme 'event-emacs)
;;; event-emacs-theme.el ends here
